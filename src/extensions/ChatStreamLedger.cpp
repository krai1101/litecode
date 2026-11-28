#include "extensions/ChatStreamLedger.h"
#include "extensions/IconWire.h"
#include "extensions/MarkdownWire.h"
#include <QJsonArray>

namespace litecode::extensions {
namespace {
bool isPosition(const QJsonValue& value) {
    if (!value.isObject())
        return false;
    const auto point = value.toObject();
    return point.value("line").isDouble() && point.value("character").isDouble() &&
           point.value("line").toInt(-1) >= 0 && point.value("character").toInt(-1) >= 0 &&
           point.value("line").toDouble() == point.value("line").toInt() &&
           point.value("character").toDouble() == point.value("character").toInt();
}

bool isRange(const QJsonValue& value) {
    if (!value.isObject())
        return false;
    const auto range = value.toObject();
    if (!isPosition(range.value("start")) || !isPosition(range.value("end")))
        return false;
    const auto start = range.value("start").toObject();
    const auto end = range.value("end").toObject();
    const int startLine = start.value("line").toInt();
    const int endLine = end.value("line").toInt();
    return startLine < endLine || (startLine == endLine && start.value("character").toInt() <=
                                                               end.value("character").toInt());
}

bool isResource(const QJsonValue& value) {
    if (value.isString())
        return !value.toString().isEmpty();
    const auto object = value.toObject();
    return value.isObject() && object.value("uri").isString() &&
           !object.value("uri").toString().isEmpty() && isRange(object.value("range"));
}

bool isIconPath(const QJsonValue& value) { return wire::decodeIconPath(value).has_value(); }

bool isMarkdown(const QJsonValue& value) {
    if (value.isString())
        return true;
    if (!value.isObject())
        return false;
    const auto markdown = wire::decodeMarkdownString(value.toObject());
    if (!markdown)
        return false;
    const auto trusted = std::get_if<bool>(&markdown->isTrusted);
    return !trusted || !*trusted;
}

bool isFileTree(const QJsonValue& value, int depth = 0) {
    if (!value.isArray() || depth > 32)
        return false;
    for (const auto& nodeValue : value.toArray()) {
        if (!nodeValue.isObject())
            return false;
        const auto node = nodeValue.toObject();
        if (node.value("name").toString().isEmpty())
            return false;
        if (node.contains("children") && !isFileTree(node.value("children"), depth + 1))
            return false;
    }
    return true;
}

bool validPart(const QJsonObject& payload) {
    const auto kind = payload.value("kind").toString();
    const auto value = payload.value("value");
    if (kind == "markdown")
        return isMarkdown(value);
    if (kind == "anchor")
        return isResource(value) &&
               (!payload.contains("title") || payload.value("title").isString());
    if (kind == "button") {
        const auto command = value.toObject();
        return value.isObject() && command.value("command").isString() &&
               !command.value("command").toString().isEmpty() &&
               command.value("title").isString() &&
               (!command.contains("tooltip") || command.value("tooltip").isString()) &&
               (!command.contains("arguments") || command.value("arguments").isArray());
    }
    if (kind == "filetree")
        return isFileTree(value) && payload.value("baseUri").isString() &&
               !payload.value("baseUri").toString().isEmpty();
    if (kind == "progress")
        return value.isString();
    if (kind == "reference")
        return isResource(value) &&
               (!payload.contains("iconPath") || isIconPath(payload.value("iconPath")));
    return false;
}
} // namespace

bool ChatStreamLedger::isValidPart(const QJsonObject& payload) { return validPart(payload); }

bool ChatStreamLedger::start(quint64 requestId) {
    if (requestId == 0 || requests_.contains(requestId))
        return false;
    requests_.insert(requestId, {});
    return true;
}

std::optional<ChatStreamLedger::Part> ChatStreamLedger::append(quint64 requestId,
                                                               const QJsonObject& payload) {
    auto it = requests_.find(requestId);
    if (it == requests_.end() || it->cancelled)
        return std::nullopt;
    if (!isValidPart(payload))
        return std::nullopt;
    return Part{it->nextSequence++, payload};
}

bool ChatStreamLedger::finish(quint64 requestId) { return requests_.remove(requestId) != 0; }

bool ChatStreamLedger::cancel(quint64 requestId) {
    auto it = requests_.find(requestId);
    if (it == requests_.end() || it->cancelled)
        return false;
    it->cancelled = true;
    return true;
}

bool ChatStreamLedger::contains(quint64 requestId) const { return requests_.contains(requestId); }

bool ChatStreamLedger::isCancelled(quint64 requestId) const {
    const auto it = requests_.constFind(requestId);
    return it != requests_.cend() && it->cancelled;
}

void ChatStreamLedger::clear() { requests_.clear(); }

} // namespace litecode::extensions
