#include "extensions/ChatHistory.h"
#include "extensions/ChatHistoryWire.h"
#include "extensions/ChatRequestWire.h"
#include "extensions/ChatStreamLedger.h"

namespace litecode::extensions {
namespace {
bool isContentPart(const QJsonObject& part) {
    const auto kind = part.value("kind").toString();
    return kind == "markdown" || kind == "filetree" || kind == "anchor" || kind == "button";
}
} // namespace

QJsonArray ChatHistory::history(const QString& participantId) const {
    return turnsByParticipant_.value(participantId);
}

bool ChatHistory::record(const QString& participantId, const QJsonObject& request,
                         const QJsonArray& responseParts, const QJsonObject& result) {
    if (participantId.isEmpty() || !request.value("references").isArray() ||
        !request.value("toolReferences").isArray())
        return false;
    const auto normalized = wire::normalizeChatRequest(request);
    if (!normalized)
        return false;

    QJsonArray content;
    for (const auto& value : responseParts) {
        if (!value.isObject())
            return false;
        const auto part = value.toObject();
        if (!ChatStreamLedger::isValidPart(part))
            return false;
        if (isContentPart(part))
            content.append(part);
    }

    QJsonObject requestTurn{{"kind", "request"},
                            {"prompt", normalized->value("prompt")},
                            {"participant", participantId},
                            {"references", normalized->value("references")},
                            {"toolReferences", normalized->value("toolReferences")}};
    QJsonObject responseTurn{{"kind", "response"},
                             {"response", content},
                             {"result", result},
                             {"participant", participantId}};
    if (normalized->contains("command")) {
        requestTurn.insert("command", normalized->value("command"));
        responseTurn.insert("command", normalized->value("command"));
    }
    if (!wire::decodeChatContext(QJsonArray{requestTurn, responseTurn}, participantId))
        return false;
    auto& turns = turnsByParticipant_[participantId];
    turns.append(requestTurn);
    turns.append(responseTurn);
    return true;
}

void ChatHistory::clear() { turnsByParticipant_.clear(); }

} // namespace litecode::extensions
