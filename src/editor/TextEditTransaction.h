#pragma once
#include "core/DocumentTypes.h"
#include <optional>

namespace litecode::editor {
struct PreparedTextEdits final {
    QByteArray contents;
    // Descending original-buffer coordinates, ready for model/widget/mirror
    // application without changing the coordinates of subsequent operations.
    QVector<core::DocumentEdit> edits;
};
std::optional<PreparedTextEdits> prepareTextEdits(const QByteArray& before,
                                                  QVector<core::DocumentEdit> edits,
                                                  QString* error = nullptr);
} // namespace litecode::editor
