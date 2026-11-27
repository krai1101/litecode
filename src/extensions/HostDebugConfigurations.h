#pragma once
#include "extensions/DebugConfigurationApi.h"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <functional>

namespace litecode::extensions {
// Host-side invocation of a single provider handle. The workbench owns chaining
// across extension hosts and variable substitution between resolution phases.
class HostDebugConfigurations final : public QObject {
  public:
    explicit HostDebugConfigurations(std::function<void(const QJsonObject&)> send, QObject* parent);
    ~HostDebugConfigurations() override;
    Disposable registerDebugConfigurationProvider(const QString& type,
        std::shared_ptr<DebugConfigurationProvider> provider, DebugConfigurationProviderTriggerKind trigger);
    void invoke(quint64 requestId, quint64 handle, const QString& method,
        const std::optional<WorkspaceFolder>& folder, const QJsonValue& configuration);
    void cancel(quint64 requestId);
    void shutdown();
  private:
    struct Registration { QString type; std::shared_ptr<DebugConfigurationProvider> provider; };
    struct Pending { quint64 handle{}; std::shared_ptr<CancellationTokenSource> cancellation; };
    void remove(quint64 handle);
    void complete(quint64 id, const QJsonValue& result, const QString& error = {});
    template<typename T> void settle(quint64 id, ProviderResult<T> result, std::shared_ptr<DebugConfigurationProvider> provider);
    std::function<void(const QJsonObject&)> send_;
    QHash<quint64, Registration> providers_;
    QHash<quint64, Pending> pending_;
    quint64 nextHandle_{};
    bool stopped_{};
};
} // namespace litecode::extensions
