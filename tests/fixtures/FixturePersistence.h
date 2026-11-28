#pragma once
#include "extensions/ExtensionApi.h"
#include <QJsonArray>
#include <stdexcept>

// Exercises the public SDK from a real native host, without storage backdoors.
inline void registerPersistenceFixture(litecode::extensions::ExtensionContext& context) {
    using namespace litecode::extensions;
    const auto secretEvents = std::make_shared<QJsonArray>();
    context.subscriptions.push_back(
        context.secrets().onDidChange()([secretEvents](const SecretStorageChangeEvent& event) {
            secretEvents->append(event.key);
        }));
    const auto prefix = context.extensionId();
    const auto configurationEvents = std::make_shared<QJsonArray>();
    context.subscriptions.push_back(context.workspace().onDidChangeConfiguration()(
        [configurationEvents](const ConfigurationChangeEvent& event) {
            if (event.affectsConfiguration("fixtureRelease.value"))
                configurationEvents->append("fixtureRelease.value");
        }));
    context.subscriptions.push_back(context.commands().registerCommand(
        prefix + ".configuration",
        [&context, configurationEvents](const QJsonArray& args) -> CommandResult {
            const auto request = args.isEmpty() ? QJsonObject{} : args.first().toObject();
            const auto config = context.workspace().getConfiguration("fixtureRelease");
            if (request.value("operation") == QJsonValue("events"))
                return *configurationEvents;
            if (request.value("operation") == QJsonValue("update")) {
                const auto target = request.value("workspace").toBool()
                                        ? ConfigurationTarget::Workspace
                                        : ConfigurationTarget::Global;
                return config->update("value", request.value("value"), target).then([] {
                    return QJsonValue(true);
                });
            }
            if (request.value("operation") != QJsonValue("read"))
                throw std::invalid_argument("Unknown configuration fixture operation.");
            QJsonObject result{{"value", config->get("value")}, {"has", config->has("value")}};
            if (const auto info = config->inspect("value")) {
                result.insert("defaultValue", info->defaultValue);
                result.insert("globalValue", info->globalValue);
                result.insert("workspaceValue", info->workspaceValue);
            }
            return result;
        }));
    context.subscriptions.push_back(context.commands().registerCommand(
        prefix + ".persistence", [&context](const QJsonArray& args) -> CommandResult {
            const auto request = args.isEmpty() ? QJsonObject{} : args.first().toObject();
            const auto operation = request.value("operation").toString();
            const auto key = request.value("key").toString("value");
            const auto complete = [] { return QJsonValue(true); };
            if (operation == "globalUpdate")
                return context.globalState().update(key, request.value("value")).then(complete);
            if (operation == "workspaceUpdate")
                return context.workspaceState().update(key, request.value("value")).then(complete);
            if (operation == "syncKeys") {
                context.globalState().setKeysForSync({key});
                return true;
            }
            if (operation != "read")
                throw std::invalid_argument("Unknown persistence fixture operation.");
            QJsonObject result{
                {"globalKeys", QJsonArray::fromStringList(context.globalState().keys())},
                {"workspaceKeys", QJsonArray::fromStringList(context.workspaceState().keys())},
                {"global", context.globalState().get(key)},
                {"workspace", context.workspaceState().get(key)},
                {"globalStorageUri", context.globalStorageUri().toString()},
                {"logUri", context.logUri().toString()}};
            if (context.storageUri())
                result.insert("storageUri", context.storageUri()->toString());
            return result;
        }));
    context.subscriptions.push_back(context.commands().registerCommand(
        prefix + ".secrets", [&context, secretEvents](const QJsonArray& args) -> CommandResult {
            const auto request = args.isEmpty() ? QJsonObject{} : args.first().toObject();
            const auto operation = request.value("operation").toString();
            const auto key = request.value("key").toString("token");
            if (operation == "store")
                return context.secrets().store(key, request.value("value").toString()).then([] {
                    return QJsonValue(true);
                });
            if (operation == "delete")
                return context.secrets().delete_(key).then([] { return QJsonValue(true); });
            if (operation == "keys")
                return context.secrets().keys().then([](const QStringList& keys) {
                    return QJsonValue(QJsonArray::fromStringList(keys));
                });
            if (operation == "events")
                return *secretEvents;
            if (operation != "get")
                throw std::invalid_argument("Unknown secret fixture operation.");
            return context.secrets().get(key).then([](const std::optional<QString>& value) {
                QJsonObject result{{"found", value.has_value()}};
                if (value)
                    result.insert("value", *value);
                return QJsonValue(result);
            });
        }));
}
