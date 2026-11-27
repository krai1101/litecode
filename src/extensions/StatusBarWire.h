#pragma once
#include "extensions/StatusBarApi.h"
#include <QJsonObject>
namespace litecode::extensions::wire {
struct StatusBarState final {
    std::optional<QString> itemId, name;
    StatusBarAlignment alignment{StatusBarAlignment::Left};
    std::optional<double> priority;
    QString text;
    std::optional<StatusBarTooltip> tooltip;
    std::optional<StatusBarColor> color;
    std::optional<ThemeColor> backgroundColor;
    std::optional<StatusBarCommand> command;
    std::optional<AccessibilityInformation> accessibilityInformation;
};
QJsonObject encodeStatusBarState(const StatusBarState& state);
std::optional<StatusBarState> decodeStatusBarState(const QJsonObject& value);
} // namespace litecode::extensions::wire
