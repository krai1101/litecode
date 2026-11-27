#include "extensions/EditorStateWire.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

namespace litecode::extensions::wire {
namespace {
bool validId(const QJsonValue& value, bool document = false) {
    if (!value.isString() || value.toString().isEmpty() || value.toString().size() > 256) return false;
    if (!document) return true;
    bool valid = false;
    const auto id = value.toString().toULongLong(&valid);
    return valid && id && QString::number(id) == value.toString();
}
std::optional<Position> position(const QJsonValue& value) {
    if (!value.isArray() || value.toArray().size() != 2) return {};
    const auto data = value.toArray();
    for (const auto& coordinate : data) {
        const auto number = coordinate.toDouble(-1);
        if (!coordinate.isDouble() || !std::isfinite(number) || number < 0
            || number > std::numeric_limits<int>::max() || std::floor(number) != number) return {};
    }
    return Position(data[0].toInt(), data[1].toInt());
}
}
QJsonObject encodeEditorState(const WindowEditorState& state) {
    QJsonArray editors;
    for (const auto& editor : state.editors) {
        QJsonArray selections;
        for (const auto& selection : editor.selections)
            selections.append(QJsonObject{{"anchor", QJsonArray{selection.anchor().line(), selection.anchor().character()}},
                {"active", QJsonArray{selection.active().line(), selection.active().character()}}});
        editors.append(QJsonObject{{"id", editor.id}, {"documentId", editor.documentId},
            {"documentVersion", QString::number(editor.documentVersion)}, {"selections", selections}});
    }
    return {{"editors", editors}, {"activeEditorId", state.activeEditorId ? QJsonValue(*state.activeEditorId) : QJsonValue(QJsonValue::Null)}};
}
std::optional<WindowEditorState> decodeEditorState(const QJsonObject& message) {
    if (!message.value("editors").isArray()) return {};
    WindowEditorState result;
    QSet<QString> ids;
    for (const auto& value : message.value("editors").toArray()) {
        if (!value.isObject()) return {};
        const auto item = value.toObject();
        bool valid = false;
        const auto version = item.value("documentVersion").toString().toLongLong(&valid);
        if (!validId(item.value("id")) || !validId(item.value("documentId"), true)
            || !valid || version <= 0 || QString::number(version) != item.value("documentVersion").toString()
            || ids.contains(item.value("id").toString()) || !item.value("selections").isArray()) return {};
        TextEditorState editor{item.value("id").toString(), item.value("documentId").toString(), version, {}};
        for (const auto& selection : item.value("selections").toArray()) {
            if (!selection.isObject()) return {};
            const auto anchor = position(selection.toObject().value("anchor"));
            const auto active = position(selection.toObject().value("active"));
            if (!anchor || !active) return {};
            editor.selections.append(Selection(*anchor, *active));
        }
        if (editor.selections.isEmpty()) return {};
        ids.insert(editor.id);
        result.editors.append(std::move(editor));
    }
    const auto active = message.value("activeEditorId");
    if (active.isString() && ids.contains(active.toString())) result.activeEditorId = active.toString();
    else if (!active.isNull()) return {};
    return result;
}
bool sameEditorState(const WindowEditorState& left, const WindowEditorState& right) {
    if (left.activeEditorId != right.activeEditorId || left.editors.size() != right.editors.size()) return false;
    for (qsizetype index = 0; index < left.editors.size(); ++index) {
        const auto& first = left.editors[index]; const auto& second = right.editors[index];
        if (first.id != second.id || first.documentId != second.documentId || first.documentVersion != second.documentVersion
            || first.selections.size() != second.selections.size()) return false;
        for (qsizetype cursor = 0; cursor < first.selections.size(); ++cursor)
            if (!first.selections[cursor].anchor().isEqual(second.selections[cursor].anchor())
                || !first.selections[cursor].active().isEqual(second.selections[cursor].active())) return false;
    }
    return true;
}
} // namespace litecode::extensions::wire
