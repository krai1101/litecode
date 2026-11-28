#pragma once
#include "CodexViewLayout.h"
#include "codex/ChatSession.h"
namespace litecode::codex {
void populateTurnPresentation(const TurnState& turn, CodexTurnView& view, int& budget,
                              bool active = false, const CodexViewState* presentation = nullptr);
QString codexFinalResponse(const TurnState& turn);
QString codexTurnDiff(const TurnState& turn);
QString codexFileDiff(const QString& diff, const QString& path);
} // namespace litecode::codex
