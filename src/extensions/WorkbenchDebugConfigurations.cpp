#include "extensions/WorkbenchDebugConfigurations.h"
#include <QJsonArray>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace litecode::extensions {
WorkbenchDebugConfigurations::WorkbenchDebugConfigurations(Send send, QObject* parent)
    : QObject(parent), send_(std::move(send)) {}
WorkbenchDebugConfigurations::~WorkbenchDebugConfigurations() {
    // Manager/host teardown performs cancellation before the transport dies.
    const auto pending = operations_.keys();
    for (const auto id : pending)
        cancel(id);
}
bool WorkbenchDebugConfigurations::registerProvider(const QString& owner,
                                                    const QJsonObject& message) {
    bool valid = false;
    const auto text = message.value("handle").toString();
    const auto handle = text.toULongLong(&valid);
    const auto type = message.value("type");
    const auto trigger = message.value("triggerKind");
    if (!valid || !handle || QString::number(handle) != text || !type.isString() ||
        type.toString().contains(QChar('\0')) || !trigger.isDouble() ||
        (trigger.toDouble() != 1 && trigger.toDouble() != 2) ||
        providers_.value(owner).contains(handle))
        return false;
    providers_[owner].insert(handle,
                             {owner, handle, type.toString(),
                              static_cast<DebugConfigurationProviderTriggerKind>(trigger.toInt())});
    return true;
}
bool WorkbenchDebugConfigurations::unregisterProvider(const QString& owner, quint64 handle) {
    auto found = providers_.find(owner);
    if (found == providers_.end() || !found->remove(handle))
        return false;
    const auto pending = operations_.keys();
    for (const auto id : pending) {
        const auto operation = operations_.constFind(id);
        if (operation == operations_.cend())
            continue;
        bool affected = false;
        for (const auto& provider : operation->chain)
            if (provider.owner == owner && provider.handle == handle) {
                affected = true;
                break;
            }
        if (affected)
            complete(id, QJsonValue(QJsonValue::Undefined),
                     "Debug configuration provider was disposed.");
    }
    return true;
}
void WorkbenchDebugConfigurations::ready(const QString& owner) {
    readyOwners_.insert(owner);
    const auto pending = operations_.keys();
    for (const auto id : pending)
        advance(id);
}
void WorkbenchDebugConfigurations::removeOwner(const QString& owner, const QString& error) {
    readyOwners_.remove(owner);
    providers_.remove(owner);
    const auto pending = operations_.keys();
    for (const auto id : pending) {
        const auto operation = operations_.constFind(id);
        if (operation == operations_.cend())
            continue;
        bool affected = operation->awaitingOwners.contains(owner);
        for (const auto& provider : operation->chain)
            if (provider.owner == owner) {
                affected = true;
                break;
            }
        if (affected)
            complete(id, QJsonValue(QJsonValue::Undefined), error);
    }
}
quint64 WorkbenchDebugConfigurations::request(const QString& type, const QString& method,
                                              const QJsonValue& configuration,
                                              const std::optional<WorkspaceFolder>& folder,
                                              const QStringList& awaitingOwners,
                                              DebugConfigurationProviderTriggerKind trigger) {
    if (nextId_ == std::numeric_limits<quint64>::max())
        return 0;
    if (trigger != DebugConfigurationProviderTriggerKind::Initial &&
        trigger != DebugConfigurationProviderTriggerKind::Dynamic)
        return 0;
    if ((method != "provideDebugConfigurations" && method != "resolveDebugConfiguration" &&
         method != "resolveDebugConfigurationWithSubstitutedVariables") ||
        (method != "provideDebugConfigurations" && !configuration.isObject()))
        return 0;
    const auto id = ++nextId_;
    Operation operation;
    operation.type = type;
    operation.method = method;
    operation.configuration = configuration;
    operation.folder = folder;
    operation.awaitingOwners = awaitingOwners;
    operation.trigger = trigger;
    operations_.insert(id, std::move(operation));
    // Even an empty provider chain must complete asynchronously, after the
    // caller has recorded the returned request ID.
    QTimer::singleShot(0, this, [this, id] { advance(id); });
    return id;
}
void WorkbenchDebugConfigurations::advance(quint64 id) {
    auto found = operations_.find(id);
    if (found == operations_.end() || found->call)
        return;
    auto& operation = *found;
    if (!operation.started) {
        for (const auto& owner : operation.awaitingOwners)
            if (!readyOwners_.contains(owner))
                return;
        for (const auto& entries : providers_)
            for (const auto& provider : entries) {
                if (provider.type != operation.type && provider.type != "*")
                    continue;
                if (operation.method == "provideDebugConfigurations" &&
                    provider.trigger != operation.trigger)
                    continue;
                operation.chain.append(provider);
            }
        std::sort(operation.chain.begin(), operation.chain.end(),
                  [](const Provider& left, const Provider& right) {
                      return left.owner == right.owner ? left.handle < right.handle
                                                       : left.owner < right.owner;
                  });
        operation.started = true;
    }
    if (operation.next == operation.chain.size()) {
        const auto result = operation.method == "provideDebugConfigurations"
                                ? QJsonValue(operation.configurations)
                                : operation.configuration;
        complete(id, result);
        return;
    }
    if (nextCall_ == std::numeric_limits<quint64>::max()) {
        complete(id, {}, "Debug configuration request IDs exhausted.");
        return;
    }
    const auto provider = operation.chain.at(operation.next++);
    const auto call = ++nextCall_;
    operation.call = call;
    calls_.insert(call, {id, provider});
    QJsonValue folder(QJsonValue::Null);
    if (operation.folder)
        folder = QJsonObject{{"uri", operation.folder->uri.toString(QUrl::FullyEncoded)},
                             {"name", operation.folder->name},
                             {"index", operation.folder->index}};
    const QJsonObject message{{"command", "invokeDebugConfigurationProvider"},
                              {"requestId", QString::number(call)},
                              {"handle", QString::number(provider.handle)},
                              {"method", operation.method},
                              {"folder", folder},
                              {"configuration", operation.configuration}};
    send_(provider.owner, message);
}
bool WorkbenchDebugConfigurations::accept(const QString& owner, const QJsonObject& message) {
    bool valid = false;
    const auto text = message.value("requestId").toString();
    const auto id = text.toULongLong(&valid);
    if (!valid || !id || QString::number(id) != text)
        return false;
    const auto call = calls_.constFind(id);
    if (call == calls_.cend())
        return true; // canceled/late response
    if (call->provider.owner != owner)
        return false;
    const auto operationId = call->operation;
    calls_.remove(id);
    auto operation = operations_.find(operationId);
    if (operation == operations_.end())
        return true;
    operation->call = 0;
    if (!message.value("error").isString()) {
        complete(operationId, {}, "Invalid debug configuration result.");
        return false;
    }
    const auto error = message.value("error").toString();
    if (!error.isEmpty()) {
        complete(operationId, {}, error);
        return true;
    }
    const auto kind = message.value("resultKind").toString();
    const auto result = message.value("result");
    if (kind == "undefined" && !message.contains("result")) {
        if (operation->method == "provideDebugConfigurations")
            advance(operationId);
        else
            complete(operationId, QJsonValue(QJsonValue::Undefined));
    } else if (kind == "null" && result.isNull()) {
        if (operation->method == "provideDebugConfigurations")
            advance(operationId);
        else
            complete(operationId, QJsonValue(QJsonValue::Null));
    } else if (kind == "value" && operation->method == "provideDebugConfigurations" &&
               result.isArray()) {
        for (const auto& configuration : result.toArray()) {
            if (!configuration.isObject()) {
                complete(operationId, {}, "Invalid provided debug configuration.");
                return false;
            }
            operation->configurations.append(configuration);
        }
        advance(operationId);
    } else if (kind == "value" && operation->method != "provideDebugConfigurations" &&
               result.isObject()) {
        operation->configuration = result;
        advance(operationId);
    } else {
        complete(operationId, {}, "Invalid debug configuration result.");
        return false;
    }
    return true;
}
void WorkbenchDebugConfigurations::complete(quint64 id, const QJsonValue& result,
                                            const QString& error) {
    const auto found = operations_.find(id);
    if (found == operations_.end())
        return;
    const auto call = calls_.take(found->call);
    const auto callId = found->call;
    operations_.erase(found);
    if (callId)
        send_(call.provider.owner, {{"command", "cancelDebugConfigurationProvider"},
                                    {"requestId", QString::number(callId)}});
    emit finished(id, result, error);
}
bool WorkbenchDebugConfigurations::cancel(quint64 id) {
    if (!operations_.contains(id))
        return false;
    complete(id, QJsonValue(QJsonValue::Undefined));
    return true;
}
} // namespace litecode::extensions
