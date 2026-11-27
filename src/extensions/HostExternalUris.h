#pragma once
#include <QObject>
#include <QJsonObject>
#include <QFuture>
#include <QPromise>
#include <QHash>
#include <QMutex>
#include <QUrl>
#include <functional>
#include <memory>
namespace litecode::extensions {
class HostExternalUris final : public QObject {
  public:
    explicit HostExternalUris(std::function<void(const QJsonObject&)> send);
    ~HostExternalUris() override;
    QFuture<bool> openExternal(const QUrl& uri);
    QFuture<QUrl> asExternalUri(const QUrl& uri);
    bool accept(const QJsonObject& message);
    void shutdown();
  private:
    struct Pending { std::shared_ptr<QPromise<QJsonValue>> promise; bool open{}; };
    QFuture<QJsonValue> request(const QUrl& uri, bool open);
    void finish(quint64 id, const QJsonValue& value, const QString& error);
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QHash<quint64, Pending> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
}
