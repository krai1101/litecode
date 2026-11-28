#pragma once
#include "extensions/EnvironmentApi.h"
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPromise>
#include <functional>
#include <memory>

namespace litecode::extensions {
class HostClipboard final : public QObject, public Clipboard {
  public:
    explicit HostClipboard(std::function<void(const QJsonObject&)> send);
    ~HostClipboard() override;
    QFuture<QString> readText() override;
    QFuture<void> writeText(const QString& value) override;
    void complete(quint64 id, const QJsonValue& result, const QString& error);
    void shutdown();

  private:
    struct Pending {
        std::shared_ptr<QPromise<QJsonValue>> promise;
        bool write{};
    };
    QFuture<QJsonValue> request(bool write, const QString& text = {});
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QHash<quint64, Pending> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
