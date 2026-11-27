#include "extensions/StatusBarWire.h"
#include "extensions/MarkdownWire.h"
#include <cmath>
#include <type_traits>
#include <utility>
namespace litecode::extensions::wire {
QJsonObject encodeStatusBarState(const StatusBarState& state) {
    QJsonObject value{{"alignment", static_cast<int>(state.alignment)}, {"text", state.text}};
    if (state.itemId) value.insert("itemId", *state.itemId);
    if (state.name) value.insert("name", *state.name);
    if (state.priority) value.insert("priority", *state.priority);
    if (state.tooltip) std::visit([&](const auto& tooltip) {
        using T = std::decay_t<decltype(tooltip)>;
        if constexpr (std::is_same_v<T, QString>) value.insert("tooltip", QJsonObject{{"kind", "text"}, {"value", tooltip}});
        else value.insert("tooltip", QJsonObject{{"kind", "markdown"}, {"value", encodeMarkdownString(tooltip)}});
    }, *state.tooltip);
    if (state.color) std::visit([&](const auto& color) {
        using T = std::decay_t<decltype(color)>;
        if constexpr (std::is_same_v<T, QString>) value.insert("color", QJsonObject{{"kind", "css"}, {"value", color}});
        else value.insert("color", QJsonObject{{"kind", "theme"}, {"value", color.id()}});
    }, *state.color);
    if (state.backgroundColor) value.insert("backgroundColor", state.backgroundColor->id());
    if (state.command) std::visit([&](const auto& command) {
        using T = std::decay_t<decltype(command)>;
        if constexpr (std::is_same_v<T, QString>) value.insert("command", QJsonObject{{"kind", "id"}, {"command", command}});
        else {
            QJsonObject encoded{{"kind", "command"}, {"command", command.command}, {"title", command.title}, {"arguments", command.arguments}};
            if (command.tooltip) encoded.insert("tooltip", *command.tooltip);
            value.insert("command", encoded);
        }
    }, *state.command);
    if (state.accessibilityInformation) {
        QJsonObject info{{"label", state.accessibilityInformation->label}};
        if (state.accessibilityInformation->role) info.insert("role", *state.accessibilityInformation->role);
        value.insert("accessibilityInformation", info);
    }
    return value;
}
std::optional<StatusBarState> decodeStatusBarState(const QJsonObject& value) {
    StatusBarState state;
    const auto alignment = value.value("alignment");
    if (!alignment.isDouble() || (alignment.toDouble() != 1 && alignment.toDouble() != 2) || !value.value("text").isString()) return {};
    state.alignment = static_cast<StatusBarAlignment>(alignment.toInt()); state.text = value.value("text").toString();
    for (const auto& [key, target] : {std::pair{"itemId", &state.itemId}, {"name", &state.name}}) {
        if (!value.contains(key)) continue;
        if (!value.value(key).isString()) return {};
        *target = value.value(key).toString();
    }
    if (value.contains("priority")) {
        if (!value.value("priority").isDouble() || !std::isfinite(value.value("priority").toDouble())) return {};
        state.priority = value.value("priority").toDouble();
    }
    if (value.contains("tooltip")) {
        if (!value.value("tooltip").isObject()) return {};
        const auto tooltip = value.value("tooltip").toObject();
        if (tooltip.value("kind") == "text" && tooltip.value("value").isString()) state.tooltip = tooltip.value("value").toString();
        else if (tooltip.value("kind") == "markdown" && tooltip.value("value").isObject()) {
            const auto markdown = decodeMarkdownString(tooltip.value("value").toObject()); if (!markdown) return {}; state.tooltip = *markdown;
        } else return {};
    }
    if (value.contains("color")) {
        if (!value.value("color").isObject()) return {};
        const auto color = value.value("color").toObject();
        if (!color.value("value").isString()) return {};
        if (color.value("kind") == "css") state.color = color.value("value").toString();
        else if (color.value("kind") == "theme") state.color = ThemeColor(color.value("value").toString());
        else return {};
    }
    if (value.contains("backgroundColor")) {
        if (!value.value("backgroundColor").isString()) return {};
        const auto color = value.value("backgroundColor").toString();
        if (color != "statusBarItem.errorBackground" && color != "statusBarItem.warningBackground") return {};
        state.backgroundColor = ThemeColor(color);
    }
    if (value.contains("command")) {
        if (!value.value("command").isObject()) return {};
        const auto command = value.value("command").toObject();
        if (!command.value("command").isString()) return {};
        if (command.value("kind") == "id") state.command = command.value("command").toString();
        else if (command.value("kind") == "command") {
            if (!command.value("title").isString() || !command.value("arguments").isArray()) return {};
            Command item{command.value("title").toString(), command.value("command").toString(), {}, command.value("arguments").toArray()};
            if (command.contains("tooltip")) { if (!command.value("tooltip").isString()) return {}; item.tooltip = command.value("tooltip").toString(); }
            state.command = item;
        } else return {};
    }
    if (value.contains("accessibilityInformation")) {
        if (!value.value("accessibilityInformation").isObject()) return {};
        const auto info = value.value("accessibilityInformation").toObject();
        if (!info.value("label").isString()) return {};
        AccessibilityInformation accessibility{info.value("label").toString(), {}};
        if (info.contains("role")) { if (!info.value("role").isString()) return {}; accessibility.role = info.value("role").toString(); }
        state.accessibilityInformation = accessibility;
    }
    return state;
}
}
