#pragma once
#include "extensions/WindowStateApi.h"
#include <QJsonObject>
#include <optional>
namespace litecode::extensions::wire {
inline QJsonObject encodeWindowState(WindowState state) {
    return {{"focused", state.focused}, {"active", state.active}};
}
inline std::optional<WindowState> decodeWindowState(const QJsonObject& state) {
    if (!state.value("focused").isBool() || !state.value("active").isBool()) return {};
    return WindowState{state.value("focused").toBool(), state.value("active").toBool()};
}
}
