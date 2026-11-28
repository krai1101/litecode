#pragma once
#include "extensions/TextApi.h"
#include <QJsonObject>
#include <QVector>
#include <optional>

namespace litecode::extensions {
// Private workbench-to-host data, not a replacement public editor API. Editor
// identity is distinct from document identity. The primary selection is first.
struct TextEditorState final {
    QString id, documentId;
    qint64 documentVersion{};
    QVector<Selection> selections;
};
struct WindowEditorState final {
    QVector<TextEditorState> editors;
    std::optional<QString> activeEditorId;
};
namespace wire {
QJsonObject encodeEditorState(const WindowEditorState& state);
std::optional<WindowEditorState> decodeEditorState(const QJsonObject& message);
bool sameEditorState(const WindowEditorState& left, const WindowEditorState& right);
} // namespace wire
} // namespace litecode::extensions
