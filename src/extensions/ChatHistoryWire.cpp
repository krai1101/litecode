#include "extensions/ChatHistoryWire.h"
#include "extensions/ChatRequestWire.h"
#include "extensions/ChatStreamLedger.h"
#include "extensions/MarkdownWire.h"

namespace litecode::extensions::wire {
namespace {
Position decodePosition(const QJsonObject& point) {
    return {point.value("line").toInt(), point.value("character").toInt()};
}

ChatResource decodeResource(const QJsonValue& value) {
    if (value.isString())
        return QUrl(value.toString());
    const auto location = value.toObject();
    const auto range = location.value("range").toObject();
    return Location{QUrl(location.value("uri").toString()),
                    {decodePosition(range.value("start").toObject()),
                     decodePosition(range.value("end").toObject())}};
}

ChatResponseFileTree decodeFileTree(const QJsonObject& value) {
    ChatResponseFileTree node{value.value("name").toString(), {}};
    if (value.contains("children")) {
        QVector<ChatResponseFileTree> children;
        for (const auto& child : value.value("children").toArray())
            children.append(decodeFileTree(child.toObject()));
        node.children = std::move(children);
    }
    return node;
}

std::optional<ChatResponseContentPart> decodeContentPart(const QJsonObject& part) {
    if (!ChatStreamLedger::isValidPart(part))
        return std::nullopt;
    const auto kind = part.value("kind").toString();
    const auto value = part.value("value");
    if (kind == "markdown") {
        MarkdownString markdown;
        if (value.isString()) {
            markdown.value = value.toString();
        } else {
            auto decoded = decodeMarkdownString(value.toObject());
            if (!decoded)
                return {};
            markdown = std::move(*decoded);
        }
        return ChatResponseMarkdownPart{std::move(markdown)};
    }
    if (kind == "filetree") {
        QVector<ChatResponseFileTree> nodes;
        for (const auto& node : value.toArray())
            nodes.append(decodeFileTree(node.toObject()));
        return ChatResponseFileTreePart{std::move(nodes), QUrl(part.value("baseUri").toString())};
    }
    if (kind == "anchor") {
        return ChatResponseAnchorPart{decodeResource(value),
                                      part.contains("title")
                                          ? std::optional<QString>(part.value("title").toString())
                                          : std::nullopt};
    }
    if (kind == "button") {
        const auto encoded = value.toObject();
        return ChatResponseCommandButtonPart{
            Command{encoded.value("title").toString(), encoded.value("command").toString(),
                    encoded.contains("tooltip")
                        ? std::optional<QString>(encoded.value("tooltip").toString())
                        : std::nullopt,
                    encoded.value("arguments").toArray()}};
    }
    // Progress and reference parts are response metadata, not turn content.
    return std::nullopt;
}

std::optional<ChatResult> decodeResult(const QJsonValue& value) {
    if (!value.isObject())
        return std::nullopt;
    const auto encoded = value.toObject();
    ChatResult result;
    if (encoded.contains("metadata")) {
        if (!encoded.value("metadata").isObject())
            return std::nullopt;
        result.metadata = encoded.value("metadata").toObject();
    }
    if (encoded.contains("errorDetails")) {
        if (!encoded.value("errorDetails").isObject())
            return std::nullopt;
        const auto error = encoded.value("errorDetails").toObject();
        if (!error.value("message").isString() ||
            (error.contains("responseIsFiltered") && !error.value("responseIsFiltered").isBool()))
            return std::nullopt;
        result.errorDetails = ChatErrorDetails{error.value("message").toString(),
                                               error.value("responseIsFiltered").toBool()};
    }
    return result;
}

std::optional<QString> optionalCommand(const QJsonObject& turn) {
    if (!turn.contains("command"))
        return std::nullopt;
    return turn.value("command").toString();
}
} // namespace

std::optional<ChatContext> decodeChatContext(const QJsonArray& history,
                                             const QString& participantId) {
    if (participantId.isEmpty() || history.size() % 2 != 0)
        return std::nullopt;
    ChatContext context;
    context.history.reserve(history.size());
    for (qsizetype index = 0; index < history.size(); index += 2) {
        if (!history.at(index).isObject() || !history.at(index + 1).isObject())
            return std::nullopt;
        const auto requestTurn = history.at(index).toObject();
        const auto responseTurn = history.at(index + 1).toObject();
        if (requestTurn.value("kind") != "request" || responseTurn.value("kind") != "response" ||
            requestTurn.value("participant") != participantId ||
            responseTurn.value("participant") != participantId ||
            !requestTurn.value("references").isArray() ||
            !requestTurn.value("toolReferences").isArray() ||
            !responseTurn.value("response").isArray() ||
            (requestTurn.contains("command") && !requestTurn.value("command").isString()) ||
            (responseTurn.contains("command") && !responseTurn.value("command").isString()) ||
            optionalCommand(requestTurn) != optionalCommand(responseTurn))
            return std::nullopt;
        const auto request = decodeChatRequest(requestTurn);
        const auto result = decodeResult(responseTurn.value("result"));
        if (!request || !result)
            return std::nullopt;
        QVector<ChatResponseContentPart> content;
        for (const auto& value : responseTurn.value("response").toArray()) {
            if (!value.isObject())
                return std::nullopt;
            const auto part = decodeContentPart(value.toObject());
            if (!part)
                return std::nullopt;
            content.append(*part);
        }
        context.history.append(ChatRequestTurn{request->prompt, participantId, request->command,
                                               request->references, request->toolReferences});
        context.history.append(ChatResponseTurn{std::move(content), *result, participantId,
                                                optionalCommand(responseTurn)});
    }
    return context;
}

} // namespace litecode::extensions::wire
