#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/ProviderApi.h"
#include "extensions/WorkspaceFolderApi.h"
#include "extensions/DebugConfigurationApi.h"
#include "extensions/DebugTrackerApi.h"
#include "extensions/DebugStartApi.h"
#include <QJsonObject>
#include <QStringList>
#include <QMap>
#include <memory>
#include <optional>
#include <variant>

namespace litecode::extensions {
struct DebugAdapterExecutableOptions final {
    std::optional<QMap<QString, QString>> env;
    std::optional<QString> cwd;
};
struct DebugAdapterExecutable final {
    QString command;
    QStringList args;
    std::optional<DebugAdapterExecutableOptions> options;
};
struct DebugAdapterServer final { int port{}; std::optional<QString> host; };
struct DebugAdapterNamedPipeServer final { QString path; };
class DebugAdapter {
  public:
    virtual ~DebugAdapter() = default;
    virtual Event<QJsonObject> onDidSendMessage() const = 0;
    virtual void handleMessage(const QJsonObject& message) = 0;
    virtual void dispose() = 0;
};
struct DebugAdapterInlineImplementation final { std::shared_ptr<DebugAdapter> implementation; };
using DebugAdapterDescriptor = std::variant<DebugAdapterExecutable, DebugAdapterServer,
    DebugAdapterNamedPipeServer, DebugAdapterInlineImplementation>;

class DebugSession {
  public:
    virtual ~DebugSession() = default;
    virtual QString id() const = 0;
    virtual QString type() const = 0;
    virtual QString name() const = 0;
    virtual void name(const QString& value) = 0;
    virtual std::shared_ptr<DebugSession> parentSession() const = 0;
    virtual std::optional<WorkspaceFolder> workspaceFolder() const = 0;
    virtual QJsonObject configuration() const = 0;
    virtual QFuture<QJsonValue> customRequest(const QString& command,
        const QJsonValue& args = QJsonValue::Undefined) = 0;
};
struct DebugSessionCustomEvent final {
    std::shared_ptr<DebugSession> session;
    QString event;
    QJsonValue body{QJsonValue::Undefined};
};
class DebugAdapterDescriptorFactory {
  public:
    virtual ~DebugAdapterDescriptorFactory() = default;
    virtual ProviderResult<DebugAdapterDescriptor> createDebugAdapterDescriptor(
        std::shared_ptr<DebugSession> session, std::optional<DebugAdapterExecutable> executable) = 0;
};
class Debug {
  public:
    virtual ~Debug() = default;
    virtual std::shared_ptr<DebugSession> activeDebugSession() const = 0;
    virtual Event<std::shared_ptr<DebugSession>> onDidStartDebugSession() const = 0;
    virtual Event<std::shared_ptr<DebugSession>> onDidTerminateDebugSession() const = 0;
    virtual Event<std::shared_ptr<DebugSession>> onDidChangeActiveDebugSession() const = 0;
    virtual Event<DebugSessionCustomEvent> onDidReceiveDebugSessionCustomEvent() const = 0;
    virtual QFuture<void> stopDebugging(std::shared_ptr<DebugSession> session = {}) = 0;
    virtual QFuture<bool> startDebugging(std::optional<WorkspaceFolder> folder,
        const std::variant<QString, QJsonObject>& nameOrConfiguration,
        const std::variant<std::shared_ptr<DebugSession>, DebugSessionOptions>& parentSessionOrOptions = DebugSessionOptions{}) = 0;
    virtual Disposable registerDebugAdapterTrackerFactory(const QString& debugType,
        std::shared_ptr<DebugAdapterTrackerFactory> factory) = 0;
    virtual Disposable registerDebugConfigurationProvider(const QString& debugType,
        std::shared_ptr<DebugConfigurationProvider> provider,
        DebugConfigurationProviderTriggerKind triggerKind = DebugConfigurationProviderTriggerKind::Initial) = 0;
    virtual Disposable registerDebugAdapterDescriptorFactory(const QString& debugType,
        std::shared_ptr<DebugAdapterDescriptorFactory> factory) = 0;
};
} // namespace litecode::extensions
