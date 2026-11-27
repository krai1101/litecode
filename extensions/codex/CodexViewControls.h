#pragma once
#include "CodexViewLayout.h"
namespace litecode::codex::view {
using extensions::NativeControl;
using extensions::NativeControlKind;
inline NativeControl control(QString id, NativeControlKind kind, QString text = {}) {
    NativeControl result;
    result.id = std::move(id);
    result.kind = kind;
    result.text = std::move(text);
    result.margin = 0;
    result.spacing = 6;
    return result;
}
inline NativeControl action(QString id, QString text, QString accessibleName, bool enabled = true) {
    auto result = control(std::move(id), NativeControlKind::Button, std::move(text));
    result.buttonStyle = extensions::NativeButtonStyle::Ghost;
    result.accessibleName = std::move(accessibleName);
    result.enabled = enabled;
    return result;
}
inline NativeControl spacer(QString id) {
    auto result = control(std::move(id), NativeControlKind::Label);
    result.stretch = 1;
    return result;
}
} // namespace litecode::codex::view
