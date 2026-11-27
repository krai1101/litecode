#include "extensions/HostClipboard.h"
#include "extensions/JsonMessageTransfer.h"
#include <QMutexLocker>
#include <QPointer>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
HostClipboard::HostClipboard(std::function<void(const QJsonObject&)> send)
    : send_(std::move(send)) {}
HostClipboard::~HostClipboard() { shutdown(); }
QFuture<QString> HostClipboard::readText() {
    return request(false).then([](const QJsonValue& value) { return value.toString(); });
}
QFuture<void> HostClipboard::writeText(const QString& value) {
    return request(true, value).then([](const QJsonValue&) {});
}
QFuture<QJsonValue> HostClipboard::request(bool write, const QString& text) {
    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();
    auto future = promise->future();
    quint64 id;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || nextId_ == std::numeric_limits<quint64>::max()) {
            promise->setException(
                std::make_exception_ptr(std::runtime_error("Clipboard service unavailable.")));
            promise->finish();
            return future;
        }
        id = ++nextId_;
        pending_.insert(id, {promise, write});
    }
    QJsonObject message{
        {"event", "clipboard"}, {"requestId", QString::number(id)}, {"write", write}};
    if (write)
        message.insert("text", text);
    QMetaObject::invokeMethod(
        this,
        [this, id, message = std::move(message)] {
            {
                QMutexLocker lock(&mutex_);
                if (stopped_ || !pending_.contains(id))
                    return;
            }
            const QPointer<HostClipboard> alive(this);
            try {
                const auto send = send_;
                wire::sendJsonTransfer(message, {"event", "clipboard", "clipboard"},
                                       [alive, send](const QJsonObject& frame) {
                                           if (!alive)
                                               throw std::runtime_error(
                                                   "Clipboard request owner stopped.");
                                           send(frame);
                                       });
            } catch (...) {
                if (alive)
                    alive->complete(id, {}, "Could not send clipboard request.");
            }
        },
        Qt::QueuedConnection);
    return future;
}
void HostClipboard::complete(quint64 id, const QJsonValue& result, const QString& error) {
    Pending pending;
    {
        QMutexLocker lock(&mutex_);
        pending = pending_.take(id);
    }
    if (!pending.promise)
        return;
    if (!error.isEmpty())
        pending.promise->setException(
            std::make_exception_ptr(std::runtime_error(error.toStdString())));
    else if ((pending.write && result.isNull()) || (!pending.write && result.isString()))
        pending.promise->addResult(result);
    else
        pending.promise->setException(
            std::make_exception_ptr(std::runtime_error("Invalid clipboard result.")));
    pending.promise->finish();
}
void HostClipboard::shutdown() {
    QHash<quint64, Pending> pending;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        pending = std::exchange(pending_, {});
    }
    for (const auto& operation : pending) {
        operation.promise->setException(std::make_exception_ptr(
            std::runtime_error("Extension host stopped before clipboard operation completed.")));
        operation.promise->finish();
    }
}
} // namespace litecode::extensions
