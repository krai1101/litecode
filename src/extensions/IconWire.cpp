#include "extensions/IconWire.h"
#include <QJsonObject>
#include <type_traits>

namespace litecode::extensions::wire {
QJsonValue encodeIconPath(const IconPath& icon) {
    return std::visit([](const auto& value) -> QJsonValue {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, QUrl>) return value.toString(QUrl::FullyEncoded);
        else if constexpr (std::is_same_v<T, LightDarkIconPath>)
            return QJsonObject{{"light", value.light.toString(QUrl::FullyEncoded)}, {"dark", value.dark.toString(QUrl::FullyEncoded)}};
        else {
            QJsonObject result{{"id", value.id()}};
            if (value.color()) result.insert("color", QJsonObject{{"id", value.color()->id()}});
            return result;
        }
    }, icon);
}
std::optional<IconPath> decodeIconPath(const QJsonValue& value) {
    const auto uri = [](const QJsonValue& value) -> std::optional<QUrl> {
        if (!value.isString()) return {};
        const QUrl result(value.toString());
        if (!result.isValid() || result.isEmpty() || result.scheme().isEmpty()) return {};
        return result;
    };
    if (value.isString()) {
        const auto resource = uri(value);
        if (resource) return IconPath{*resource};
        return {};
    }
    if (!value.isObject()) return {};
    const auto icon = value.toObject();
    if (icon.contains("light") || icon.contains("dark")) {
        const auto light = uri(icon.value("light")), dark = uri(icon.value("dark"));
        if (!light || !dark) return {};
        return IconPath{LightDarkIconPath{*light, *dark}};
    }
    if (!icon.value("id").isString()) return {};
    std::optional<ThemeColor> color;
    if (icon.contains("color")) {
        if (!icon.value("color").isObject() || !icon.value("color").toObject().value("id").isString()) return {};
        color = ThemeColor{icon.value("color").toObject().value("id").toString()};
    }
    return IconPath{ThemeIcon{icon.value("id").toString(), std::move(color)}};
}
} // namespace litecode::extensions::wire
