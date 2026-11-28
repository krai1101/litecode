#include "extensions/HostInputBox.h"
#include "extensions/ExtensionWireProtocol.h"
#include "extensions/InputBoxWire.h"
#include <QFutureWatcher>
#include <QMutexLocker>
#include <QPointer>
#include <limits>
#include <stdexcept>
namespace litecode::extensions {
namespace {
quint64 identifier(const QJsonValue& value) {
    if (!value.isString())
        return 0;
    bool valid = false;
    const auto text = value.toString();
    const auto result = text.toULongLong(&valid);
    return valid && result && QString::number(result) == text ? result : 0;
}
} // namespace
HostInputBox::HostInputBox(std::function<void(const QJsonObject&)> send) : send_(std::move(send)) {}
HostInputBox::~HostInputBox() { shutdown(); }
QFuture<std::optional<QString>> HostInputBox::show(const InputBoxOptions& options,
                                                   CancellationToken token) {
    auto promise = std::make_shared<QPromise<std::optional<QString>>>();
    promise->start();
    const auto future = promise->future();
    if (token.isCancellationRequested()) {
        promise->addResult(std::nullopt);
        promise->finish();
        return future;
    }
    quint64 id;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || nextId_ == std::numeric_limits<quint64>::max()) {
            promise->setException(
                std::make_exception_ptr(std::runtime_error("Input box service unavailable.")));
            promise->finish();
            return future;
        }
        id = ++nextId_;
        pending_.insert(id, {promise, options.validateInput, {}, false});
    }
    const QPointer<HostInputBox> self(this);
    auto cancellation = token.onCancellationRequested([self, id] {
        if (self)
            QMetaObject::invokeMethod(
                self.data(),
                [self, id] {
                    if (self)
                        self->cancel(id);
                },
                Qt::QueuedConnection);
    });
    bool attached = false;
    {
        QMutexLocker lock(&mutex_);
        auto found = pending_.find(id);
        if (found != pending_.end()) {
            found->cancellation = std::move(cancellation);
            attached = true;
        }
    }
    if (!attached)
        cancellation.dispose();
    const QJsonObject message{{"event", "inputBox"},
                              {"op", "show"},
                              {"requestId", QString::number(id)},
                              {"options", wire::encodeInputBoxOptions(options)},
                              {"hasValidation", bool(options.validateInput)}};
    QMetaObject::invokeMethod(
        this,
        [self, id, token, message] {
            if (!self)
                return;
            if (token.isCancellationRequested()) {
                self->cancel(id);
                return;
            }
            std::function<void(const QJsonObject&)> send;
            {
                QMutexLocker lock(&self->mutex_);
                auto found = self->pending_.find(id);
                if (self->stopped_ || found == self->pending_.end())
                    return;
                found->sent = true;
                send = self->send_;
            }
            try {
                if (wire::encode(message).isEmpty())
                    throw std::length_error("Input box message exceeds the transport limit.");
                send(message);
            } catch (...) {
                if (self)
                    self->fail(id, "Could not send input box request.");
            }
        },
        Qt::QueuedConnection);
    return future;
}
void HostInputBox::complete(quint64 id, const QJsonValue& value, const QString& error) {
    Pending pending;
    {
        QMutexLocker lock(&mutex_);
        pending = pending_.take(id);
    }
    if (!pending.promise)
        return;
    pending.cancellation.dispose();
    try {
        if (!error.isEmpty())
            throw std::runtime_error(error.toStdString());
        if (value.isNull())
            pending.promise->addResult(std::nullopt);
        else if (value.isString())
            pending.promise->addResult(value.toString());
        else
            throw std::runtime_error("Invalid input box result.");
    } catch (...) {
        pending.promise->setException(std::current_exception());
    }
    pending.promise->finish();
}
void HostInputBox::cancel(quint64 id) {
    bool sent;
    std::function<void(const QJsonObject&)> send;
    {
        QMutexLocker lock(&mutex_);
        const auto found = pending_.constFind(id);
        if (found == pending_.cend())
            return;
        sent = found->sent;
        send = send_;
    }
    const QPointer<HostInputBox> self(this);
    if (sent) {
        try {
            send({{"event", "inputBox"}, {"op", "cancel"}, {"requestId", QString::number(id)}});
        } catch (...) {
        }
    }
    if (self)
        self->complete(id, QJsonValue(QJsonValue::Null), {});
}
void HostInputBox::fail(quint64 id, const QString& error) {
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
    // Detach the promise first: a synchronous cancel reply must not turn this
    // transport failure into successful user cancellation.
    if (pending.sent) {
        try {
            send({{"event", "inputBox"}, {"op", "cancel"}, {"requestId", QString::number(id)}});
        } catch (...) {
        }
    }
    pending.promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    pending.promise->finish();
}
bool HostInputBox::accept(const QJsonObject& message) {
    const auto id = identifier(message.value("requestId"));
    if (!id)
        return false;
    const auto op = message.value("op").toString();
    if (op == "result") {
        if (!message.value("error").isString() ||
            (!message.value("value").isNull() && !message.value("value").isString()))
            return false;
        complete(id, message.value("value"), message.value("error").toString());
        return true;
    }
    if (op != "validate" || !message.value("value").isString())
        return false;
    const auto sequence = identifier(message.value("sequence"));
    if (!sequence)
        return false;
    validate(id, sequence, message.value("value").toString());
    return true;
}
void HostInputBox::validate(quint64 id, quint64 sequence, const QString& value) {
    std::function<InputBoxValidatorResult(const QString&)> validator;
    {
        QMutexLocker lock(&mutex_);
        const auto found = pending_.constFind(id);
        if (stopped_ || found == pending_.cend())
            return;
        validator = found->validator;
    }
    const QPointer<HostInputBox> self(this);
    try {
        auto result = validator
                          ? validator(value)
                          : InputBoxValidatorResult(InputBoxValidationResult(std::monostate{}));
        if (!self)
            return;
        if (const auto* immediate = std::get_if<InputBoxValidationResult>(&result)) {
            self->validationReply(id, sequence, *immediate);
            return;
        }
        auto* watcher = new QFutureWatcher<InputBoxValidationResult>(self.data());
        const QPointer<QFutureWatcher<InputBoxValidationResult>> aliveWatcher(watcher);
        connect(watcher, &QFutureWatcher<InputBoxValidationResult>::finished, self.data(),
                [self, aliveWatcher, id, sequence] {
                    if (!aliveWatcher)
                        return;
                    try {
                        const auto value = aliveWatcher->result();
                        if (self)
                            self->validationReply(id, sequence, value);
                    } catch (...) {
                        if (self)
                            self->validationReply(id, sequence, {}, "Input validation failed.");
                    }
                    if (aliveWatcher)
                        aliveWatcher->deleteLater();
                });
        watcher->setFuture(std::get<QFuture<InputBoxValidationResult>>(result));
    } catch (...) {
        if (self)
            self->validationReply(id, sequence, {}, "Input validation failed.");
    }
}
void HostInputBox::validationReply(quint64 id, quint64 sequence,
                                   const InputBoxValidationResult& result, const QString& error) {
    std::function<void(const QJsonObject&)> send;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || !pending_.contains(id))
            return;
        send = send_;
    }
    const QPointer<HostInputBox> self(this);
    try {
        const auto encoded = wire::encodeInputBoxValidation(result);
        if (!wire::decodeInputBoxValidation(encoded))
            throw std::invalid_argument("Invalid input validation severity.");
        send({{"event", "inputBox"},
              {"op", "validation"},
              {"requestId", QString::number(id)},
              {"sequence", QString::number(sequence)},
              {"result", encoded},
              {"error", error}});
    } catch (...) {
        if (self)
            self->fail(id, "Could not send input validation response.");
    }
}
void HostInputBox::shutdown() {
    QHash<quint64, Pending> pending;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        pending.swap(pending_);
    }
    for (auto& request : pending) {
        request.cancellation.dispose();
        request.promise->setException(std::make_exception_ptr(
            std::runtime_error("Extension host stopped before input box completed.")));
        request.promise->finish();
    }
}
} // namespace litecode::extensions
