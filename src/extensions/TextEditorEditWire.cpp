#include "extensions/TextEditorEditWire.h"
#include <QJsonArray>
#include <cmath>
#include <limits>

namespace litecode::extensions::wire {
namespace {
QJsonArray encodePosition(const Position& position) { return {position.line(), position.character()}; }
std::optional<Position> decodePosition(const QJsonValue& value) {
    if (!value.isArray()) return {};
    const auto coordinates = value.toArray();
    if (coordinates.size() != 2) return {};
    for (const auto& coordinate : coordinates) {
        if (!coordinate.isDouble()) return {};
        const auto number = coordinate.toDouble();
        if (!std::isfinite(number) || number < 0 || number > std::numeric_limits<int>::max()
            || std::floor(number) != number) return {};
    }
    return Position(coordinates[0].toInt(), coordinates[1].toInt());
}
}
QJsonObject encodeTextEditorEdit(const TextEditorEditData& data) {
    if (data.documentVersionId < 1 || (data.setEndOfLine && *data.setEndOfLine != EndOfLine::LF
            && *data.setEndOfLine != EndOfLine::CRLF) || textEditorEditRangesOverlap(data.edits))
        throw std::invalid_argument("Invalid text editor edit transaction.");
    QJsonArray edits;
    for (const auto& edit : data.edits) {
        edits.append(QJsonObject{{"start", encodePosition(edit.range.start())},
            {"end", encodePosition(edit.range.end())},
            {"text", edit.text ? QJsonValue(*edit.text) : QJsonValue(QJsonValue::Null)},
            {"forceMoveMarkers", edit.forceMoveMarkers}});
    }
    QJsonObject result{{"documentVersionId", QString::number(data.documentVersionId)},
        {"edits", edits}, {"undoStopBefore", data.undoStopBefore}, {"undoStopAfter", data.undoStopAfter}};
    if (data.setEndOfLine) result.insert("setEndOfLine", static_cast<int>(*data.setEndOfLine));
    return result;
}
std::optional<TextEditorEditData> decodeTextEditorEdit(const QJsonObject& message) {
    if (!message.value("documentVersionId").isString() || !message.value("edits").isArray()
        || !message.value("undoStopBefore").isBool() || !message.value("undoStopAfter").isBool()) return {};
    const auto versionText = message.value("documentVersionId").toString();
    bool valid = false;
    const auto version = versionText.toLongLong(&valid);
    if (!valid || version < 1 || QString::number(version) != versionText) return {};
    TextEditorEditData result;
    result.documentVersionId = version;
    result.undoStopBefore = message.value("undoStopBefore").toBool();
    result.undoStopAfter = message.value("undoStopAfter").toBool();
    if (message.contains("setEndOfLine")) {
        const auto value = message.value("setEndOfLine");
        if (!value.isDouble() || (value.toDouble() != 1 && value.toDouble() != 2)) return {};
        result.setEndOfLine = static_cast<EndOfLine>(value.toInt());
    }
    const auto edits = message.value("edits").toArray();
    result.edits.reserve(edits.size());
    for (const auto& value : edits) {
        if (!value.isObject()) return {};
        const auto edit = value.toObject();
        const auto start = decodePosition(edit.value("start"));
        const auto end = decodePosition(edit.value("end"));
        const auto text = edit.value("text");
        if (!start || !end || start->isAfter(*end) || (!text.isString() && !text.isNull())
            || !edit.value("forceMoveMarkers").isBool()) return {};
        result.edits.append({Range(*start, *end), text.isNull() ? std::nullopt : std::optional(text.toString()),
            edit.value("forceMoveMarkers").toBool()});
    }
    if (textEditorEditRangesOverlap(result.edits)) return {};
    return result;
}
} // namespace litecode::extensions::wire
