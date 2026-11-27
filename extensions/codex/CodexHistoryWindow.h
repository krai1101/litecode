#pragma once
#include <QString>
#include <QVector>
namespace litecode::codex {
struct TurnState;
struct CodexViewState;
void prependCodexHistoryWindow(CodexViewState& state, const QVector<TurnState>& turns,
                               const QString& anchor);
void appendCodexHistoryWindow(CodexViewState& state, const QVector<TurnState>& turns);
void projectCodexHistoryWindow(CodexViewState& state, const QVector<TurnState>& turns,
                               bool hasOlder);
QString codexOlderWindowEnd(const QVector<TurnState>& turns, const QString& firstVisibleId,
                            bool includeAnchor);
} // namespace litecode::codex
