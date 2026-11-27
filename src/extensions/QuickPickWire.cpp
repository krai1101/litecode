#include "extensions/QuickPickWire.h"
#include "extensions/IconWire.h"
namespace litecode::extensions::wire {
QJsonObject encodeQuickPickOptions(const QuickPickOptions& options) {
    QJsonObject result;
    for (const auto& [name, value] : {std::pair{"title", options.title}, {"placeHolder", options.placeHolder}, {"prompt", options.prompt}})
        if (value) result.insert(name, *value);
    for (const auto& [name, value] : {std::pair{"matchOnDescription", options.matchOnDescription}, {"matchOnDetail", options.matchOnDetail},
        {"ignoreFocusOut", options.ignoreFocusOut}, {"canPickMany", options.canPickMany}}) if (value) result.insert(name, *value);
    return result;
}
std::optional<QuickPickOptions> decodeQuickPickOptions(const QJsonObject& object) {
    QuickPickOptions result;
    for (const auto& [name, target] : {std::pair{"title", &result.title}, {"placeHolder", &result.placeHolder}, {"prompt", &result.prompt}}) {
        if (!object.contains(name)) continue;
        if (!object.value(name).isString()) return {};
        *target = object.value(name).toString();
    }
    for (const auto& [name, target] : {std::pair{"matchOnDescription", &result.matchOnDescription}, {"matchOnDetail", &result.matchOnDetail},
        {"ignoreFocusOut", &result.ignoreFocusOut}, {"canPickMany", &result.canPickMany}}) {
        if (!object.contains(name)) continue;
        if (!object.value(name).isBool()) return {};
        *target = object.value(name).toBool();
    }
    return result;
}
QJsonArray encodeQuickPickItems(const QVector<QuickPickItem>& items) {
    QJsonArray result;
    for (const auto& item : items) {
        QJsonObject value{{"label", item.label}};
        if (item.kind) value.insert("kind", static_cast<int>(*item.kind));
        if (item.kind.value_or(QuickPickItemKind::Default) != QuickPickItemKind::Separator) {
            if (item.description) value.insert("description", *item.description);
            if (item.detail) value.insert("detail", *item.detail);
            if (item.iconPath) value.insert("iconPath", encodeIconPath(*item.iconPath));
            if (item.resourceUri) value.insert("resourceUri", item.resourceUri->toString(QUrl::FullyEncoded));
            if (item.picked) value.insert("picked", *item.picked);
            if (item.alwaysShow) value.insert("alwaysShow", *item.alwaysShow);
        }
        // Item buttons are explicitly not rendered by showQuickPick.
        result.append(value);
    }
    return result;
}
std::optional<QVector<QuickPickItem>> decodeQuickPickItems(const QJsonArray& array) {
    QVector<QuickPickItem> result;
    for (const auto& value : array) {
        if (!value.isObject()) return {};
        const auto object = value.toObject();
        if (!object.value("label").isString()) return {};
        QuickPickItem item; item.label = object.value("label").toString();
        if (object.contains("kind")) {
            const auto kind = object.value("kind");
            if (!kind.isDouble() || (kind.toDouble() != 0 && kind.toDouble() != -1)) return {};
            item.kind = static_cast<QuickPickItemKind>(kind.toInt());
        }
        if (item.kind.value_or(QuickPickItemKind::Default) != QuickPickItemKind::Separator) {
            for (const auto& [name, target] : {std::pair{"description", &item.description}, {"detail", &item.detail}}) {
                if (!object.contains(name)) continue;
                if (!object.value(name).isString()) return {};
                *target = object.value(name).toString();
            }
            for (const auto& [name, target] : {std::pair{"picked", &item.picked}, {"alwaysShow", &item.alwaysShow}}) {
                if (!object.contains(name)) continue;
                if (!object.value(name).isBool()) return {};
                *target = object.value(name).toBool();
            }
            if (object.contains("iconPath")) { item.iconPath = decodeIconPath(object.value("iconPath")); if (!item.iconPath) return {}; }
            if (object.contains("resourceUri")) {
                if (!object.value("resourceUri").isString()) return {};
                QUrl uri(object.value("resourceUri").toString(), QUrl::StrictMode);
                if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty()) return {};
                item.resourceUri = uri;
            }
        }
        result.append(item);
    }
    return result;
}
} // namespace litecode::extensions::wire
