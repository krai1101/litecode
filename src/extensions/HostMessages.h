#pragma once
#include "extensions/MessageApi.h"
#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPromise>
#include <QVector>
#include <functional>
#include <memory>

namespace litecode::extensions {
class HostMessages final : public QObject {
  public:
    explicit HostMessages(std::function<void(const QJsonObject&)> send);
    ~HostMessages() override;
    QFuture<std::optional<int>> show(int severity, const QString& message,
                                     const MessageOptions& options,
                                     const QVector<MessageItem>& items);
    void complete(quint64 id, const QJsonValue& selection, const QString& error);
    void shutdown();

  private:
    struct Pending {
        std::shared_ptr<QPromise<std::optional<int>>> promise;
        int itemCount{};
    };
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QHash<quint64, Pending> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
