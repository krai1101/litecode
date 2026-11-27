#pragma once
#include "editor/ScintillaEditor.h"
#include "ui/extensions/TextEditorEditPreparation.h"

namespace litecode::ui {
std::optional<QVector<extensions::Selection>> readEditorSelections(const QByteArray& contents,
    core::LineEnding eol, const editor::EditorSelectionState& selections);
// Translate actual widget selections through EOL conversion and the complete
// UTF-16 marker transaction, then back to UTF-8 widget positions. No live writes.
std::optional<editor::EditorSelectionState> prepareEditorSelections(
    const QByteArray& before, const editor::EditorSelectionState& selections,
    const PreparedTextEditorEdit& edit, QString* error = nullptr);
} // namespace litecode::ui
