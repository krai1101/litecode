#pragma once
#include <QJsonObject>

namespace litecode::codex {
// Describes literal tool invocations without executing orchestration code.
QJsonObject toolCallDisplay(const QJsonObject& payload);
} // namespace litecode::codex
