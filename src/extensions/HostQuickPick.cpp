#include "extensions/HostQuickPick.h"
#include "extensions/ExtensionWireProtocol.h"
#include "extensions/QuickPickWire.h"
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <limits>
#include <stdexcept>
namespace litecode::extensions {
HostQuickPick::HostQuickPick(std::function<void(const QJsonObject&)> send)
    : send_(std::move(send)) {}
HostQuickPick::~HostQuickPick() { shutdown(); }
detail::QuickPickSession HostQuickPick::begin(const QuickPickOptions& options,
                                              CancellationToken token,
                                              std::function<void(int)> focus) {
    auto promise = std::make_shared<QPromise<std::optional<QVector<int>>>>();
    promise->start();
    detail::QuickPickSession session;
    session.result = promise->future();
    const QPointer<HostQuickPick> self(this);
    quint64 id = 0;
    {
        QMutexLocker lock(&mutex_);
        if (!stopped_ && nextId_ != std::numeric_limits<quint64>::max() &&
            !token.isCancellationRequested()) {
            id = ++nextId_;
            pending_.insert(id, {promise,
                                 std::move(focus),
                                 {},
                                 {},
                                 options.canPickMany.value_or(false),
                                 false,
                                 false});
        }
    }
    session.setItems = [self, id](QVector<QuickPickItem> items) {
        if (self && id)
            QMetaObject::invokeMethod(
                self.data(),
                [self, id, items = std::move(items)]() mutable {
                    if (!self)
                        return;
                    try {
                        self->setItems(id, std::move(items));
                    } catch (...) {
                        if (self)
                            self->finish(id, {}, std::current_exception(), true);
                    }
                },
                Qt::QueuedConnection);
    };
    session.fail = [self, id](std::exception_ptr error) {
        if (self && id)
            QMetaObject::invokeMethod(
                self.data(),
                [self, id, error] {
                    if (!self)
                        return;
                    try {
                        if (error)
                            std::rethrow_exception(error);
                    } catch (const CancellationError&) {
                        self->finish(id, {}, {}, true);
                        return;
                    } catch (...) {
                    }
                    self->finish(id, {}, error, true);
                },
                Qt::QueuedConnection);
    };
    if (!id) {
        if (token.isCancellationRequested())
            promise->addResult(std::nullopt);
        else
            promise->setException(
                std::make_exception_ptr(std::runtime_error("Quick pick service unavailable.")));
        promise->finish();
        return session;
    }
    auto registration = token.onCancellationRequested([self, id] {
        if (self)
            QMetaObject::invokeMethod(
                self.data(),
                [self, id] {
                    if (self)
                        self->finish(id, {}, {}, true);
                },
                Qt::QueuedConnection);
    });
    bool attached = false;
    {
        QMutexLocker lock(&mutex_);
        auto found = pending_.find(id);
        if (found != pending_.end()) {
            found->cancellation = std::move(registration);
            attached = true;
        }
    }
    if (!attached)
        registration.dispose();
    const auto encoded = wire::encodeQuickPickOptions(options);
    QMetaObject::invokeMethod(
        this,
        [self, id, token, encoded] {
            if (!self)
                return;
            if (token.isCancellationRequested()) {
                self->finish(id, {}, {}, true);
                return;
            }
            {
                QMutexLocker lock(&self->mutex_);
                auto found = self->pending_.find(id);
                if (found == self->pending_.end())
                    return;
                found->shown = true;
            }
            self->transmit(id, {{"event", "quickPick"},
                                {"op", "show"},
                                {"requestId", QString::number(id)},
                                {"options", encoded}});
        },
        Qt::QueuedConnection);
    return session;
}
void HostQuickPick::transmit(quint64 id, const QJsonObject& message) {
    std::function<void(const QJsonObject&)> send;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || !pending_.contains(id))
            return;
        send = send_;
    }
    const QPointer<HostQuickPick> self(this);
    try {
        if (wire::encode(message).isEmpty())
            throw std::length_error("Quick pick message exceeds the transport limit.");
        send(message);
    } catch (...) {
        if (self)
            self->finish(
                id, {},
                std::make_exception_ptr(std::runtime_error("Could not send quick pick request.")),
                true);
    }
}
void HostQuickPick::setItems(quint64 id, QVector<QuickPickItem> items) {
    const auto encoded = wire::encodeQuickPickItems(items);
    if (!wire::decodeQuickPickItems(encoded)) {
        finish(id, {}, std::make_exception_ptr(std::invalid_argument("Invalid quick pick items.")),
               true);
        return;
    }
    {
        QMutexLocker lock(&mutex_);
        auto found = pending_.find(id);
        if (stopped_ || found == pending_.end())
            return;
        found->items = std::move(items);
        found->loaded = true;
    }
    transmit(id, {{"event", "quickPick"},
                  {"op", "items"},
                  {"requestId", QString::number(id)},
                  {"items", encoded}});
}
void HostQuickPick::finish(quint64 id, std::optional<QVector<int>> selection,
                           std::exception_ptr error, bool dismiss) {
    Pending pending;
    std::function<void(const QJsonObject&)> send;
    {
        QMutexLocker lock(&mutex_);
        pending = pending_.take(id);
        send = send_;
    }
    if (!pending.promise)
        return;
    pending.cancellation.dispose();
    if (dismiss && pending.shown) {
        try {
            send({{"event", "quickPick"}, {"op", "cancel"}, {"requestId", QString::number(id)}});
        } catch (...) {
        }
    }
    if (error)
        pending.promise->setException(error);
    else
        pending.promise->addResult(std::move(selection));
    pending.promise->finish();
}
bool HostQuickPick::accept(const QJsonObject& message) {
    bool valid = false;
    const auto text = message.value("requestId").toString();
    const auto id = text.toULongLong(&valid);
    if (!valid || !id || QString::number(id) != text)
        return false;
    const auto op = message.value("op").toString();
    if (op != "focus" && op != "result")
        return false;
    if (op == "result") {
        if (!message.value("error").isString())
            return false;
        if (!message.value("error").toString().isEmpty()) {
            finish(id, {},
                   std::make_exception_ptr(
                       std::runtime_error(message.value("error").toString().toStdString())));
            return true;
        }
    }
    const auto selection = message.value("selection");
    std::function<void(int)> focus;
    std::optional<QVector<int>> indices;
    {
        QMutexLocker lock(&mutex_);
        const auto found = pending_.constFind(id);
        if (found == pending_.cend())
            return true;
        const auto selectable = [&](const QJsonValue& value) {
            return value.isDouble() && value.toDouble() == value.toInt(-1) &&
                   value.toInt(-1) >= 0 && value.toInt() < found->items.size() &&
                   found->items.at(value.toInt()).kind.value_or(QuickPickItemKind::Default) !=
                       QuickPickItemKind::Separator;
        };
        if (op == "focus") {
            if (!selectable(selection))
                return false;
            focus = found->focus;
        } else if (!selection.isNull()) {
            if (!selection.isArray() || !found->loaded)
                return false;
            const auto array = selection.toArray();
            if (!found->many && array.size() != 1)
                return false;
            QVector<int> values;
            QSet<int> unique;
            for (const auto& value : array) {
                if (!selectable(value) || unique.contains(value.toInt()))
                    return false;
                unique.insert(value.toInt());
                values.append(value.toInt());
            }
            indices = std::move(values);
        }
    }
    if (op == "focus") {
        if (focus) {
            try {
                focus(selection.toInt());
            } catch (...) {
            }
        }
    } else
        finish(id, std::move(indices));
    return true;
}
void HostQuickPick::shutdown() {
    QHash<quint64, Pending> pending;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        pending.swap(pending_);
    }
    for (auto& value : pending) {
        value.cancellation.dispose();
        value.promise->setException(std::make_exception_ptr(
            std::runtime_error("Extension host stopped before quick pick completed.")));
        value.promise->finish();
    }
}
} // namespace litecode::extensions
