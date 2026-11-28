#include "extensions/HostExternalUris.h"
#include "extensions/ExtensionWireProtocol.h"
#include "extensions/UriWire.h"
#include <QMutexLocker>
#include <QPointer>
#include <limits>
#include <stdexcept>
namespace litecode::extensions {
HostExternalUris::HostExternalUris(std::function<void(const QJsonObject&)> send)
    : send_(std::move(send)) {}
HostExternalUris::~HostExternalUris() { shutdown(); }
QFuture<bool> HostExternalUris::openExternal(const QUrl& uri) {
    return request(uri, true).then([](const QJsonValue& result) { return result.toBool(); });
}
QFuture<QUrl> HostExternalUris::asExternalUri(const QUrl& uri) {
    return request(uri, false).then([](const QJsonValue& result) {
        return *wire::decodeUri(result);
    });
}
QFuture<QJsonValue> HostExternalUris::request(const QUrl& uri, bool open) {
    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();
    const auto future = promise->future();
    QString error;
    if (!uri.isValid() || uri.scheme().trimmed().isEmpty())
        error = "Invalid URI scheme.";
    else if (open && uri.scheme().compare("command", Qt::CaseInsensitive) == 0)
        error = "Invalid URI scheme 'command'.";
    quint64 id = 0;
    {
        QMutexLocker lock(&mutex_);
        if (error.isEmpty() && (stopped_ || nextId_ == std::numeric_limits<quint64>::max()))
            error = "External URI service unavailable.";
        if (error.isEmpty()) {
            id = ++nextId_;
            pending_.insert(id, {promise, open});
        }
    }
    if (!error.isEmpty()) {
        promise->setException(std::make_exception_ptr(std::invalid_argument(error.toStdString())));
        promise->finish();
        return future;
    }
    const QJsonObject message{{"event", "externalUri"},
                              {"requestId", QString::number(id)},
                              {"open", open},
                              {"uri", wire::encodeUri(uri)}};
    const QPointer<HostExternalUris> self(this);
    QMetaObject::invokeMethod(
        this,
        [self, id, message] {
            if (!self)
                return;
            std::function<void(const QJsonObject&)> send;
            {
                QMutexLocker lock(&self->mutex_);
                if (self->stopped_ || !self->pending_.contains(id))
                    return;
                send = self->send_;
            }
            try {
                if (wire::encode(message).isEmpty())
                    throw std::length_error("URI exceeds transport limit.");
                send(message);
            } catch (...) {
                if (self)
                    self->finish(id, {}, "Could not send external URI request.");
            }
        },
        Qt::QueuedConnection);
    return future;
}
void HostExternalUris::finish(quint64 id, const QJsonValue& value, const QString& error) {
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
    else if ((pending.open && value.isBool()) || (!pending.open && wire::decodeUri(value)))
        pending.promise->addResult(value);
    else
        pending.promise->setException(
            std::make_exception_ptr(std::runtime_error("Invalid external URI result.")));
    pending.promise->finish();
}
bool HostExternalUris::accept(const QJsonObject& message) {
    const auto text = message.value("requestId").toString();
    bool valid = false;
    const auto id = text.toULongLong(&valid);
    if (!valid || !id || QString::number(id) != text || !message.value("error").isString())
        return false;
    finish(id, message.value("result"), message.value("error").toString());
    return true;
}
void HostExternalUris::shutdown() {
    QHash<quint64, Pending> pending;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        pending.swap(pending_);
        send_ = {};
    }
    for (const auto& value : pending) {
        value.promise->setException(std::make_exception_ptr(
            std::runtime_error("Extension host stopped before URI operation completed.")));
        value.promise->finish();
    }
}
} // namespace litecode::extensions
