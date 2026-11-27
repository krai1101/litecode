#include "extensions/HostMessages.h"
#include <QJsonArray>
#include <QMutexLocker>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
HostMessages::HostMessages(std::function<void(const QJsonObject&)> send) : send_(std::move(send)) {}
HostMessages::~HostMessages() { shutdown(); }
QFuture<std::optional<int>> HostMessages::show(int severity, const QString& text,
    const MessageOptions& options, const QVector<MessageItem>& items) {
    auto promise = std::make_shared<QPromise<std::optional<int>>>();
    promise->start();
    const auto future = promise->future();
    if (severity < 1 || severity > 3 || items.size() > std::numeric_limits<int>::max()) {
        promise->setException(std::make_exception_ptr(std::invalid_argument("Invalid message arguments.")));
        promise->finish();
        return future;
    }
    quint64 id;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || nextId_ == std::numeric_limits<quint64>::max()) {
            promise->setException(std::make_exception_ptr(std::runtime_error("Message service unavailable.")));
            promise->finish();
            return future;
        }
        id = ++nextId_;
        pending_.insert(id, {promise, static_cast<int>(items.size())});
    }
    QJsonArray actions;
    for (const auto& item : items) actions.append(QJsonObject{{"title", item.title}, {"close", item.isCloseAffordance.value_or(false)}});
    QJsonObject message{{"event", "windowMessage"}, {"requestId", QString::number(id)},
        {"severity", severity}, {"message", text}, {"modal", options.modal.value_or(false)}, {"items", actions}};
    if (options.detail) message.insert("detail", *options.detail);
    QMetaObject::invokeMethod(this, [this, id, message = std::move(message)] {
        { QMutexLocker lock(&mutex_); if (stopped_ || !pending_.contains(id)) return; }
        try { send_(message); }
        catch (...) { complete(id, {}, "Could not send window message."); }
    }, Qt::QueuedConnection);
    return future;
}
void HostMessages::complete(quint64 id, const QJsonValue& selection, const QString& error) {
    Pending pending;
    { QMutexLocker lock(&mutex_); pending = pending_.take(id); }
    if (!pending.promise) return;
    if (!error.isEmpty()) pending.promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    else if (selection.isNull()) pending.promise->addResult(std::nullopt);
    else if (selection.isDouble() && selection.toDouble() == selection.toInt(-1) &&
             selection.toInt(-1) >= 0 && selection.toInt() < pending.itemCount)
        pending.promise->addResult(std::optional<int>(selection.toInt()));
    else pending.promise->setException(std::make_exception_ptr(std::runtime_error("Invalid window message selection.")));
    pending.promise->finish();
}
void HostMessages::shutdown() {
    QHash<quint64, Pending> pending;
    { QMutexLocker lock(&mutex_); if (stopped_) return; stopped_ = true; pending = std::exchange(pending_, {}); }
    for (const auto& request : pending) {
        request.promise->setException(std::make_exception_ptr(std::runtime_error("Extension host stopped before message completed.")));
        request.promise->finish();
    }
}
} // namespace litecode::extensions
