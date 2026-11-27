#pragma once
#include "ui/extensions/TextEditorEditPreparation.h"

namespace litecode::ui {
// Private UTF-16 absolute offsets. Direction and original empty-selection state
// survive the entire transaction, not just one operation.
struct EditorSelectionOffsets final { qint64 anchor{}, active{}; };
EditorSelectionOffsets trackEditorSelection(EditorSelectionOffsets selection,
    const QVector<PreparedEditorOperation>& operations);
} // namespace litecode::ui
