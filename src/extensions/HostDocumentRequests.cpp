#include "extensions/HostDocumentRequests.h"
#include <QMutexLocker>
#include <QThread>
#include <stdexcept>

namespace litecode::extensions {
HostDocumentRequests::HostDocumentRequests(std::function<void(const QJsonObject&)> send)
    : send_(std::move(send)) {}
HostDocumentRequests::~HostDocumentRequests() { shutdown(); }
QFuture<bool> HostDocumentRequests::save(const QString& documentId) {
    auto promise = std::make_shared<QPromise<bool>>();
    promise->start();
    const auto future = promise->future();
    quint64 id;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_) {
            promise->setException(
                std::make_exception_ptr(std::runtime_error("Document service stopped.")));
            promise->finish();
            return future;
        }
        id = ++nextId_;
        pending_.insert(id, promise);
    }
    QMetaObject::invokeMethod(
        this,
        [this, id, documentId] {
            {
                QMutexLocker lock(&mutex_);
                if (stopped_ || !pending_.contains(id))
                    return;
            }
            try {
                send_({{"event", "documentSave"},
                       {"requestId", QString::number(id)},
                       {"documentId", documentId}});
            } catch (...) {
                complete(id, false, "Could not send document save request.");
            }
        },
        Qt::QueuedConnection);
    return future;
}
void HostDocumentRequests::complete(quint64 id, bool saved, const QString& error) {
    std::shared_ptr<QPromise<bool>> promise;
    {
        QMutexLocker lock(&mutex_);
        promise = pending_.take(id);
    }
    if (!promise)
        return;
    if (!error.isEmpty())
        promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    else
        promise->addResult(saved);
    promise->finish();
}
void HostDocumentRequests::shutdown() {
    QHash<quint64, std::shared_ptr<QPromise<bool>>> pending;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        pending = std::exchange(pending_, {});
    }
    for (const auto& promise : pending) {
        promise->setException(std::make_exception_ptr(
            std::runtime_error("Extension host stopped before document save completed.")));
        promise->finish();
    }
}
} // namespace litecode::extensions
