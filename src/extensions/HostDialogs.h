#pragma once
#include "extensions/DialogApi.h"
#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPromise>
#include <functional>
#include <memory>

namespace litecode::extensions {
class HostDialogs final : public QObject {
  public:
    explicit HostDialogs(std::function<void(const QJsonObject&)> send);
    ~HostDialogs() override;
    QFuture<std::optional<QVector<QUrl>>> showOpenDialog(const OpenDialogOptions& options = {});
    QFuture<std::optional<QUrl>> showSaveDialog(const SaveDialogOptions& options = {});
    void complete(quint64 id, const QJsonValue& resources, const QString& error);
    void shutdown();

  private:
    QFuture<std::optional<QVector<QUrl>>> request(bool save, const QJsonObject& options);
    std::function<void(const QJsonObject&)> send_;
    struct Pending {
        std::shared_ptr<QPromise<std::optional<QVector<QUrl>>>> promise;
        bool save{};
        bool many{};
    };
    QMutex mutex_;
    QHash<quint64, Pending> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
