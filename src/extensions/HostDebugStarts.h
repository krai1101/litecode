#pragma once
#include "extensions/DebugApi.h"
#include <QObject>
#include <QHash>
#include <QPromise>
#include <functional>

namespace litecode::extensions {
class HostDebugStarts final : public QObject {
  public:
    explicit HostDebugStarts(std::function<void(const QJsonObject&)> send, QObject* parent);
    ~HostDebugStarts() override;
    QFuture<bool> start(std::optional<WorkspaceFolder> folder,
        const std::variant<QString, QJsonObject>& configuration,
        const std::variant<std::shared_ptr<DebugSession>, DebugSessionOptions>& options);
    bool accept(const QJsonObject& message);
    void shutdown();
  private:
    std::function<void(const QJsonObject&)> send_;
    QHash<quint64, std::shared_ptr<QPromise<bool>>> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
