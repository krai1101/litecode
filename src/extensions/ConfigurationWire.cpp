#include "extensions/ConfigurationWire.h"

#include <QJsonArray>

namespace litecode::extensions::wire {
namespace {

QJsonObject encodeLayer(const ConfigurationLayer& layer) {
    QJsonArray overrides;
    for (const auto& group : layer.overrides)
        overrides.append(QJsonObject{{"identifiers", QJsonArray::fromStringList(group.identifiers)},
                                     {"contents", group.contents},
                                     {"keys", QJsonArray::fromStringList(group.keys)}});
    return {{"contents", layer.contents},
            {"keys", QJsonArray::fromStringList(layer.keys)},
            {"overrides", overrides}};
}

bool strings(const QJsonValue& value, QStringList& result) {
    if (!value.isArray())
        return false;
    for (const auto& item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty() || result.contains(item.toString()))
            return false;
        result.append(item.toString());
    }
    return true;
}

bool decodeLayer(const QJsonValue& value, ConfigurationLayer& layer) {
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    if (!object.value("contents").isObject() || !object.value("overrides").isArray() ||
        !strings(object.value("keys"), layer.keys))
        return false;
    layer.contents = object.value("contents").toObject();
    for (const auto& key : layer.keys)
        if (layer.value(key).isUndefined())
            return false;
    for (const auto& entry : object.value("overrides").toArray()) {
        if (!entry.isObject())
            return false;
        const auto group = entry.toObject();
        ConfigurationOverride decoded;
        if (!group.value("contents").isObject() ||
            !strings(group.value("identifiers"), decoded.identifiers) ||
            decoded.identifiers.isEmpty() || !strings(group.value("keys"), decoded.keys))
            return false;
        decoded.contents = group.value("contents").toObject();
        const ConfigurationLayer tree{decoded.contents, decoded.keys, {}};
        for (const auto& key : decoded.keys)
            if (tree.value(key).isUndefined())
                return false;
        layer.overrides.append(std::move(decoded));
    }
    return true;
}

} // namespace

QJsonObject encodeConfigurationModel(const ConfigurationModel& model) {
    QJsonArray folders;
    for (const auto& folder : model.folders)
        folders.append(QJsonObject{{"uri", folder.uri.toString(QUrl::FullyEncoded)},
                                   {"settings", encodeLayer(folder.settings)}});
    return {{"defaults", encodeLayer(model.defaults)},
            {"global", encodeLayer(model.global)},
            {"workspace", encodeLayer(model.workspace)},
            {"folders", folders}};
}

std::optional<ConfigurationModel> decodeConfigurationModel(const QJsonObject& message,
                                                           QString* error) {
    if (error)
        error->clear();
    const auto fail = [error]() -> std::optional<ConfigurationModel> {
        if (error)
            *error = "Invalid configuration snapshot.";
        return std::nullopt;
    };
    ConfigurationModel model;
    if (!decodeLayer(message.value("defaults"), model.defaults) ||
        !decodeLayer(message.value("global"), model.global) ||
        !decodeLayer(message.value("workspace"), model.workspace) ||
        !message.value("folders").isArray())
        return fail();
    for (const auto& entry : message.value("folders").toArray()) {
        if (!entry.isObject())
            return fail();
        const auto folder = entry.toObject();
        ConfigurationFolder decoded;
        if (!folder.value("uri").isString())
            return fail();
        decoded.uri = QUrl(folder.value("uri").toString(), QUrl::StrictMode);
        if (!decoded.uri.isValid() || decoded.uri.isRelative() ||
            !decodeLayer(folder.value("settings"), decoded.settings))
            return fail();
        model.folders.append(std::move(decoded));
    }
    return model;
}

} // namespace litecode::extensions::wire
