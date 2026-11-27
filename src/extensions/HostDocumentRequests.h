#pragma once
#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPromise>
#include <functional>
#include <memory>
#include <utility>

namespace litecode::extensions {
class HostDocumentRequests final : public QObject {
  public:
    explicit HostDocumentRequests(std::function<void(const QJsonObject&)> send);
    ~HostDocumentRequests() override;
    QFuture<bool> save(const QString& documentId);
    void complete(quint64 requestId, bool saved, const QString& error);
    void shutdown();

  private:
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QHash<quint64, std::shared_ptr<QPromise<bool>>> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
