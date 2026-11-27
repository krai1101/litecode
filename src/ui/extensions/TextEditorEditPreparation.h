#pragma once
#include "editor/TextDocumentModel.h"
#include "extensions/TextEditorEditData.h"

namespace litecode::ui {
struct PreparedEditorOperation final {
    core::DocumentEdit edit;
    bool forceMoveMarkers{};
    qint64 utf16Offset{}, removedUtf16Length{}, insertedUtf16Length{};
};
struct PreparedTextEditorEdit final {
    editor::TextDocumentModel replacement;
    std::optional<core::DocumentEdit> eolEdit;
    // Widget operations retain marker flags and coincident insertion order,
    // including content-identical operations. Model events contain only changes.
    QVector<PreparedEditorOperation> operations;
    QVector<core::DocumentEdit> contentChanges;
    bool undoStopBefore{}, undoStopAfter{};
};
// Stages all validation, coordinate conversion and allocation on a copied model.
// The caller checks editor identity/attachment and commits only after widget
// application is ready. This function never publishes or mutates the live model.
std::optional<PreparedTextEditorEdit> prepareTextEditorEdit(
    const editor::TextDocumentModel& model, const extensions::TextEditorEditData& data,
    QString* error = nullptr);
} // namespace litecode::ui
