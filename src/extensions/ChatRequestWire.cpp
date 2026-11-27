#include "extensions/ChatRequestWire.h"
#include <QJsonArray>
#include <algorithm>

namespace litecode::extensions::wire {
namespace {
bool isNonnegativeInteger(const QJsonValue& value) {
    return value.isDouble() && value.toDouble() >= 0 && value.toDouble() == value.toInt(-1);
}

bool isPromptRange(const QJsonValue& value, qsizetype promptLength) {
    if (!value.isArray())
        return false;
    const auto range = value.toArray();
    return range.size() == 2 && isNonnegativeInteger(range.at(0)) &&
           isNonnegativeInteger(range.at(1)) && range.at(0).toInt() <= range.at(1).toInt() &&
           range.at(1).toInt() <= promptLength;
}

bool isPosition(const QJsonValue& value) {
    if (!value.isObject())
        return false;
    const auto point = value.toObject();
    return isNonnegativeInteger(point.value("line")) &&
           isNonnegativeInteger(point.value("character"));
}

bool isLocationRange(const QJsonValue& value) {
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

bool isReferenceValue(const QJsonObject& reference) {
    const auto value = reference.value("value");
    if (value.isUndefined())
        return false;
    if (!reference.contains("$litecodeValueType"))
        return true;
    const auto type = reference.value("$litecodeValueType").toString();
    if (type == "uri")
        return value.isString() && !value.toString().isEmpty() && QUrl(value.toString()).isValid();
    if (type != "location" || !value.isObject())
        return false;
    const auto location = value.toObject();
    const auto uri = location.value("uri");
    return uri.isString() && !uri.toString().isEmpty() && QUrl(uri.toString()).isValid() &&
           isLocationRange(location.value("range"));
}

bool isReference(const QJsonValue& value, qsizetype promptLength) {
    if (!value.isObject())
        return false;
    const auto reference = value.toObject();
    return reference.value("id").isString() && !reference.value("id").toString().isEmpty() &&
           reference.contains("value") && isReferenceValue(reference) &&
           (!reference.contains("range") ||
            isPromptRange(reference.value("range"), promptLength)) &&
           (!reference.contains("modelDescription") ||
            reference.value("modelDescription").isString());
}

bool isToolReference(const QJsonValue& value, qsizetype promptLength) {
    if (!value.isObject())
        return false;
    const auto reference = value.toObject();
    return reference.value("name").isString() && !reference.value("name").toString().isEmpty() &&
           (!reference.contains("range") || isPromptRange(reference.value("range"), promptLength));
}

QJsonArray reversePromptOrder(const QJsonArray& input) {
    QVector<QJsonObject> ordered;
    ordered.reserve(input.size());
    for (const auto& item : input)
        ordered.append(item.toObject());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const QJsonObject& left, const QJsonObject& right) {
                         const auto leftRange = left.value("range").toArray();
                         const auto rightRange = right.value("range").toArray();
                         if (leftRange.isEmpty())
                             return false;
                         if (rightRange.isEmpty())
                             return true;
                         if (leftRange.at(0).toInt() != rightRange.at(0).toInt())
                             return leftRange.at(0).toInt() > rightRange.at(0).toInt();
                         return leftRange.at(1).toInt() > rightRange.at(1).toInt();
                     });
    QJsonArray result;
    for (const auto& item : ordered)
        result.append(item);
    return result;
}

std::optional<std::pair<int, int>> decodeRange(const QJsonObject& object) {
    if (!object.contains("range"))
        return {};
    const auto range = object.value("range").toArray();
    return std::pair<int, int>{range.at(0).toInt(), range.at(1).toInt()};
}

ChatPromptReferenceValue decodeValue(const QJsonObject& reference) {
    const auto value = reference.value("value");
    const auto type = reference.value("$litecodeValueType").toString();
    if (type == "uri")
        return QUrl(value.toString());
    if (type == "location") {
        const auto object = value.toObject();
        const auto range = object.value("range").toObject();
        const auto start = range.value("start").toObject();
        const auto end = range.value("end").toObject();
        return Location{QUrl(object.value("uri").toString()),
                        {{start.value("line").toInt(), start.value("character").toInt()},
                         {end.value("line").toInt(), end.value("character").toInt()}}};
    }
    if (value.isString())
        return value.toString();
    return value;
}
} // namespace

std::optional<QJsonObject> normalizeChatRequest(const QJsonObject& input) {
    if (!input.value("prompt").isString() ||
        (input.contains("command") && !input.value("command").isString()) ||
        (input.contains("references") && !input.value("references").isArray()) ||
        (input.contains("toolReferences") && !input.value("toolReferences").isArray()))
        return std::nullopt;
    const auto promptLength = input.value("prompt").toString().size();
    const auto references = input.value("references").toArray();
    const auto toolReferences = input.value("toolReferences").toArray();
    for (const auto& reference : references)
        if (!isReference(reference, promptLength))
            return std::nullopt;
    for (const auto& reference : toolReferences)
        if (!isToolReference(reference, promptLength))
            return std::nullopt;
    QJsonObject result{{"prompt", input.value("prompt")}};
    if (input.contains("command"))
        result.insert("command", input.value("command"));
    result.insert("references", reversePromptOrder(references));
    result.insert("toolReferences", reversePromptOrder(toolReferences));
    return result;
}

std::optional<ChatRequest> decodeChatRequest(const QJsonObject& input) {
    const auto normalized = normalizeChatRequest(input);
    if (!normalized)
        return std::nullopt;
    ChatRequest result;
    result.prompt = normalized->value("prompt").toString();
    if (normalized->contains("command"))
        result.command = normalized->value("command").toString();
    for (const auto& item : normalized->value("references").toArray()) {
        const auto reference = item.toObject();
        result.references.append(
            {reference.value("id").toString(), decodeRange(reference),
             reference.contains("modelDescription")
                 ? std::optional<QString>(reference.value("modelDescription").toString())
                 : std::nullopt,
             decodeValue(reference)});
    }
    for (const auto& item : normalized->value("toolReferences").toArray()) {
        const auto reference = item.toObject();
        result.toolReferences.append({reference.value("name").toString(), decodeRange(reference)});
    }
    return result;
}

} // namespace litecode::extensions::wire
