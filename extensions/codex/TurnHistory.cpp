#include "TurnHistory.h"
#include "ChatSession.h"
namespace litecode::codex {
void readTurnHistory(const QJsonObject& object, TurnState& turn) {
    turn.id = object.value("id").toString();
    turn.status = object.value("status").toString();
    turn.startedMs = object.value("startedAt").toInteger() * 1000;
    turn.durationMs = object.value("durationMs").toInteger(-1);
    if (turn.durationMs < 0 && turn.startedMs > 0 && object.value("completedAt").isDouble())
        turn.durationMs =
            qMax<qint64>(0, object.value("completedAt").toInteger() * 1000 - turn.startedMs);
    turn.error = object.value("error").toObject().value("message").toString();
    turn.items.clear();
    turn.activeTools.clear();
    for (const auto& value : object.value("items").toArray()) {
        const auto item = value.toObject();
        turn.items.append(item);
        const auto status = item.value("status").toString();
        const auto type = item.value("type").toString();
        if ((status == "inProgress" || status == "in_progress") && type != "agentMessage" &&
            type != "reasoning" && type != "userMessage")
            turn.activeTools.insert(item.value("id").toString());
    }
}
} // namespace litecode::codex
