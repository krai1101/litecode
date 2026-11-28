#pragma once
#include "ChatSession.h"
#include <QFuture>
namespace litecode::codex {
QString codexTurnMarkdown(const TurnState& turn);
QFuture<QString> exportCodexMarkdown(AppServer& server, const QString& threadId,
                                    const QString& title);
} // namespace litecode::codex
