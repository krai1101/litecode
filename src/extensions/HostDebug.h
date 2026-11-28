#pragma once
#include "extensions/DebugApi.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <functional>

namespace litecode::extensions {
class HostInlineDebugAdapter;
class HostDebugConfigurations;
class HostDebugSessions;
class HostDebugTrackers;
class HostDebugStarts;
class HostDebug final : public QObject, public Debug {
  public:
    explicit HostDebug(std::function<void(const QJsonObject&)> send);
    ~HostDebug() override;
    Disposable
    registerDebugConfigurationProvider(const QString& debugType,
                                       std::shared_ptr<DebugConfigurationProvider> provider,
                                       DebugConfigurationProviderTriggerKind triggerKind) override;
    HostDebugConfigurations& configurations();
    HostDebugSessions& sessions();
    HostDebugTrackers& trackers();
    HostDebugStarts& starts();
    QFuture<bool>
    startDebugging(std::optional<WorkspaceFolder> folder,
                   const std::variant<QString, QJsonObject>& configuration,
                   const std::variant<std::shared_ptr<DebugSession>, DebugSessionOptions>& options =
                       DebugSessionOptions{}) override;
    Disposable registerDebugAdapterTrackerFactory(
        const QString& type, std::shared_ptr<DebugAdapterTrackerFactory> factory) override;
    std::shared_ptr<DebugSession> activeDebugSession() const override;
    Event<std::shared_ptr<DebugSession>> onDidStartDebugSession() const override;
    Event<std::shared_ptr<DebugSession>> onDidTerminateDebugSession() const override;
    Event<std::shared_ptr<DebugSession>> onDidChangeActiveDebugSession() const override;
    Event<DebugSessionCustomEvent> onDidReceiveDebugSessionCustomEvent() const override;
    QFuture<void> stopDebugging(std::shared_ptr<DebugSession> session = {}) override;
    Disposable registerDebugAdapterDescriptorFactory(
        const QString& debugType, std::shared_ptr<DebugAdapterDescriptorFactory> factory) override;
    void resolve(quint64 requestId, const QJsonObject& session,
                 const std::optional<DebugAdapterExecutable>& executable);
    void shutdown();
    void release(quint64 requestId);

  private:
    struct Factory {
        std::shared_ptr<DebugAdapterDescriptorFactory> provider;
        quint64 token{};
    };
    struct Pending {
        QString type;
        quint64 token{};
    };
    void complete(quint64 requestId, const std::optional<DebugAdapterDescriptor>& descriptor,
                  const QString& error);
    void remove(const QString& type, quint64 token);
    template <typename T>
    void watch(quint64 requestId, const QFuture<T>& future,
               std::shared_ptr<DebugAdapterDescriptorFactory> provider);
    std::function<void(const QJsonObject&)> send_;
    QHash<QString, Factory> factories_;
    QHash<quint64, Pending> pending_;
    QHash<quint64, QPointer<HostInlineDebugAdapter>> inlineAdapters_;
    quint64 nextToken_{};
    bool stopped_{};
    std::unique_ptr<HostDebugConfigurations> configurations_;
    std::unique_ptr<HostDebugSessions> sessions_;
    std::unique_ptr<HostDebugTrackers> trackers_;
    std::unique_ptr<HostDebugStarts> starts_;
};
} // namespace litecode::extensions
