#pragma once
#include <QJsonObject>
namespace litecode::codex {
struct TurnState;
void readTurnHistory(const QJsonObject& object, TurnState& turn);
} // namespace litecode::codex
