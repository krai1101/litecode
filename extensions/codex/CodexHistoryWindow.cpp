#include "CodexHistoryWindow.h"
#include "CodexTurnPresentation.h"
#include <QCryptographicHash>
namespace litecode::codex {
void projectCodexHistoryWindow(CodexViewState& state, const QVector<TurnState>& turns,
                               bool hasOlder) {
    state.turns.clear();
    const int windowSize = qBound(8, state.historyWindowSize, 32);
    int budget = windowSize * 16 * 1024;
    int end = turns.size();
    if (!state.turnWindowEndId.isEmpty())
        for (int i = 0; i < turns.size(); ++i)
            if (turns[i].id == state.turnWindowEndId) {
                end = i + 1;
                break;
            }
    int first = end;
    for (int i = end - 1; i >= 0 && state.turns.size() < windowSize && budget > 0; --i) {
        first = i;
        const auto& turn = turns[i];
        CodexTurnView view;
        view.turnId = turn.id;
        view.key = QString::fromLatin1(
            QCryptographicHash::hash(turn.id.toUtf8(), QCryptographicHash::Sha256).toHex());
        view.error = turn.error.left(4096);
        view.undoAvailable = !turn.diff.isEmpty();
        // Patches are opened in the editor, not embedded into chat. Keeping
        // their bounded signature must not consume the message display budget.
        view.diff = codexTurnDiff(turn).left(64 * 1024);
        const bool active =
            i == turns.size() - 1 && (turn.status == "inProgress" || turn.status == "in_progress");
        populateTurnPresentation(turn, view, budget, active, &state);
        state.turns.prepend(std::move(view));
    }
    state.hasOlder = hasOlder || first > 0;
}
namespace {
int turnIndex(const QVector<TurnState>& turns, const QString& id) {
    for (int i = 0; i < turns.size(); ++i)
        if (turns[i].id == id)
            return i;
    return -1;
}
void setWindowEnd(CodexViewState& state, const QVector<TurnState>& turns, int end) {
    state.turnWindowEndId = end == turns.size() - 1 ? QString{} : turns[end].id;
}
} // namespace
void prependCodexHistoryWindow(CodexViewState& state, const QVector<TurnState>& turns,
                               const QString& anchor) {
    const int first = turnIndex(turns, anchor);
    if (first <= 0 || state.turns.isEmpty())
        return;
    const int last = turnIndex(turns, state.turns.last().turnId);
    if (last < first)
        return;
    state.historyWindowSize = qMin(32, state.historyWindowSize + 8);
    const int desiredFirst = qMax(0, first - 8);
    setWindowEnd(state, turns, qMin(last, desiredFirst + state.historyWindowSize - 1));
    // A very large expanded message can still fill the bounded native frame.
    // Never let it cause the same history range to be selected repeatedly.
    auto candidate = state;
    projectCodexHistoryWindow(candidate, turns, true);
    if (!candidate.turns.isEmpty() && turnIndex(turns, candidate.turns.first().turnId) >= first)
        state.turnWindowEndId = codexOlderWindowEnd(turns, anchor, false);
}
void appendCodexHistoryWindow(CodexViewState& state, const QVector<TurnState>& turns) {
    if (state.turns.isEmpty())
        return;
    const int last = turnIndex(turns, state.turns.last().turnId);
    if (last < 0)
        return;
    state.historyWindowSize = qMin(32, state.historyWindowSize + 8);
    setWindowEnd(state, turns, qMin(last + 8, int(turns.size()) - 1));
}
QString codexOlderWindowEnd(const QVector<TurnState>& turns, const QString& firstVisibleId,
                            bool includeAnchor) {
    for (int i = 1; i < turns.size(); ++i)
        if (turns[i].id == firstVisibleId)
            // An oversized single turn cannot overlap both windows: including
            // it again would select the same window forever.
            return turns[includeAnchor ? i : i - 1].id;
    return {};
}
} // namespace litecode::codex
