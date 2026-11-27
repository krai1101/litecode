#include "extensions/DialogWire.h"
#include <QJsonArray>
#include <QSet>

namespace litecode::extensions::wire {
namespace {
template<typename T>
QJsonObject encodeCommon(const T& options) {
    QJsonObject result;
    if (options.defaultUri) result.insert("defaultUri", options.defaultUri->toString(QUrl::FullyEncoded));
    if (options.title) result.insert("title", *options.title);
    if (options.filters) {
        QJsonArray filters;
        for (const auto& [name, extensions] : *options.filters) {
            QJsonArray values;
            for (const auto& extension : extensions) values.append(extension);
            filters.append(QJsonObject{{"name", name}, {"extensions", values}});
        }
        result.insert("filters", filters);
    }
    return result;
}
bool stringOption(const QJsonObject& object, const QString& key, std::optional<QString>& value) {
    if (!object.contains(key)) return true;
    if (!object.value(key).isString()) return false;
    value = object.value(key).toString();
    return true;
}
bool boolOption(const QJsonObject& object, const QString& key, std::optional<bool>& value) {
    if (!object.contains(key)) return true;
    if (!object.value(key).isBool()) return false;
    value = object.value(key).toBool();
    return true;
}
template<typename T>
bool decodeCommon(const QJsonObject& object, T& options) {
    if (!stringOption(object, "title", options.title)) return false;
    if (object.contains("defaultUri")) {
        if (!object.value("defaultUri").isString()) return false;
        const QUrl uri(object.value("defaultUri").toString(), QUrl::StrictMode);
        if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty()) return false;
        options.defaultUri = uri;
    }
    if (object.contains("filters")) {
        if (!object.value("filters").isArray()) return false;
        QVector<std::pair<QString, QStringList>> filters;
        QSet<QString> names;
        for (const auto& entry : object.value("filters").toArray()) {
            if (!entry.isObject()) return false;
            const auto filter = entry.toObject();
            if (!filter.value("name").isString() || !filter.value("extensions").isArray()) return false;
            const auto name = filter.value("name").toString();
            if (names.contains(name)) return false;
            names.insert(name);
            QStringList extensions;
            for (const auto& extension : filter.value("extensions").toArray()) {
                if (!extension.isString()) return false;
                extensions.append(extension.toString());
            }
            filters.append({name, extensions});
        }
        options.filters = std::move(filters);
    }
    return true;
}
} // namespace
QJsonObject encodeOpenDialogOptions(const OpenDialogOptions& options) {
    auto result = encodeCommon(options);
    if (options.openLabel) result.insert("openLabel", *options.openLabel);
    if (options.canSelectFiles) result.insert("canSelectFiles", *options.canSelectFiles);
    if (options.canSelectFolders) result.insert("canSelectFolders", *options.canSelectFolders);
    if (options.canSelectMany) result.insert("canSelectMany", *options.canSelectMany);
    return result;
}
QJsonObject encodeSaveDialogOptions(const SaveDialogOptions& options) {
    auto result = encodeCommon(options);
    if (options.saveLabel) result.insert("saveLabel", *options.saveLabel);
    return result;
}
std::optional<OpenDialogOptions> decodeOpenDialogOptions(const QJsonObject& object) {
    OpenDialogOptions options;
    if (!decodeCommon(object, options) || !stringOption(object, "openLabel", options.openLabel) ||
        !boolOption(object, "canSelectFiles", options.canSelectFiles) ||
        !boolOption(object, "canSelectFolders", options.canSelectFolders) ||
        !boolOption(object, "canSelectMany", options.canSelectMany)) return {};
    return options;
}
std::optional<SaveDialogOptions> decodeSaveDialogOptions(const QJsonObject& object) {
    SaveDialogOptions options;
    if (!decodeCommon(object, options) || !stringOption(object, "saveLabel", options.saveLabel)) return {};
    return options;
}
} // namespace litecode::extensions::wire
