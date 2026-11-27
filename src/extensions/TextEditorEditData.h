#pragma once
#include "extensions/TextApi.h"
#include <QVector>

namespace litecode::extensions {
// Private transport data, not WorkspaceEdit: null deletion text, marker movement
// and the two undo stops must survive the host/workbench boundary independently.
struct TextEditorEditOperation final {
    Range range;
    std::optional<QString> text;
    bool forceMoveMarkers{};
};
struct TextEditorEditData final {
    qint64 documentVersionId{};
    QVector<TextEditorEditOperation> edits;
    std::optional<EndOfLine> setEndOfLine;
    bool undoStopBefore{true}, undoStopAfter{true};
    [[nodiscard]] bool empty() const { return edits.isEmpty() && !setEndOfLine; }
};
// True permits touching boundaries/coincident insertions, but not nested ranges.
// The operation list is never reordered by this check.
[[nodiscard]] bool textEditorEditRangesOverlap(const QVector<TextEditorEditOperation>& edits);
} // namespace litecode::extensions
