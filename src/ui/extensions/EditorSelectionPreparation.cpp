#include "ui/extensions/EditorSelectionPreparation.h"
#include "ui/extensions/EditorSelectionTracking.h"
#include "extensions/DocumentTextBuffer.h"
#include <QHash>
#include <algorithm>

namespace litecode::ui {
namespace {
QHash<qint64, qint64> mapOffsets(const QString& text, QVector<qint64> offsets,
    bool fromBytes, std::optional<core::LineEnding> newEol = {}) {
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    QHash<qint64, qint64> result;
    result.reserve(offsets.size());
    qsizetype index = 0;
    qint64 source = 0, destination = 0;
    for (const auto target : offsets) {
        while (index < text.size()) {
            const auto character = text[index];
            qint64 units = 1, bytes = character.unicode() < 0x80 ? 1 : character.unicode() < 0x800 ? 2 : 3;
            const bool newline = character == '\r' || character == '\n';
            if (character.isHighSurrogate()) { units = 2; bytes = 4; }
            else if (character == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
                units = bytes = 2;
            const auto sourceStep = fromBytes ? bytes : units;
            if (source + sourceStep > target) break;
            source += sourceStep;
            destination += fromBytes ? (newline && newEol ? (*newEol == core::LineEnding::CrLf ? 2 : 1) : units) : bytes;
            index += units;
        }
        // An offset inside a surrogate pair or CRLF snaps to the preceding
        // legal caret position, as the editor's coordinate validation does.
        result.insert(target, destination);
    }
    return result;
}
}
std::optional<QVector<extensions::Selection>> readEditorSelections(const QByteArray& contents,
    core::LineEnding eol, const editor::EditorSelectionState& selections) {
    if (selections.selections.isEmpty() || selections.mainSelection < 0
        || selections.mainSelection >= selections.selections.size()) return {};
    const auto text = QString::fromUtf8(contents);
    if (text.toUtf8() != contents) return {};
    QVector<qint64> endpoints;
    for (const auto& selection : selections.selections) {
        if (selection.anchor < 0 || selection.active < 0 || selection.anchor > contents.size()
            || selection.active > contents.size()) return {};
        endpoints.append(selection.anchor); endpoints.append(selection.active);
    }
    const auto offsets = mapOffsets(text, std::move(endpoints), true);
    extensions::DocumentTextBuffer buffer(text, eol == core::LineEnding::CrLf
        ? extensions::EndOfLine::CRLF : extensions::EndOfLine::LF);
    QVector<extensions::Selection> result;
    result.reserve(selections.selections.size());
    const auto append = [&](qsizetype index) {
        const auto& selection = selections.selections[index];
        result.append(extensions::Selection(buffer.positionAt(offsets.value(selection.anchor)),
            buffer.positionAt(offsets.value(selection.active))));
    };
    append(selections.mainSelection);
    for (qsizetype index = 0; index < selections.selections.size(); ++index)
        if (index != selections.mainSelection) append(index);
    return result;
}
std::optional<editor::EditorSelectionState> prepareEditorSelections(const QByteArray& before,
    const editor::EditorSelectionState& selections, const PreparedTextEditorEdit& edit, QString* error) {
    if (error) error->clear();
    const auto fail = [&]() -> std::optional<editor::EditorSelectionState> {
        if (error) *error = QStringLiteral("Editor selection is invalid or no longer available.");
        return {};
    };
    if (selections.selections.isEmpty() || selections.mainSelection < 0
        || selections.mainSelection >= selections.selections.size()) return fail();
    QVector<qint64> offsets;
    offsets.reserve(selections.selections.size() * 2);
    for (const auto& selection : selections.selections) {
        if (selection.anchor < 0 || selection.active < 0 || selection.anchor > before.size()
            || selection.active > before.size()) return fail();
        offsets.append(selection.anchor); offsets.append(selection.active);
    }
    const auto original = QString::fromUtf8(before);
    if (original.toUtf8() != before) return fail();
    const auto initial = mapOffsets(original, std::move(offsets), true,
        edit.eolEdit ? std::optional(edit.replacement.session().lineEnding()) : std::nullopt);
    QVector<EditorSelectionOffsets> tracked;
    tracked.reserve(selections.selections.size());
    offsets.clear();
    for (const auto& selection : selections.selections) {
        const auto moved = trackEditorSelection({initial.value(selection.anchor), initial.value(selection.active)}, edit.operations);
        tracked.append(moved); offsets.append(moved.anchor); offsets.append(moved.active);
    }
    const auto final = mapOffsets(QString::fromUtf8(edit.replacement.contents()), std::move(offsets), false);
    editor::EditorSelectionState result;
    result.mainSelection = selections.mainSelection;
    result.selections.reserve(tracked.size());
    for (const auto& selection : tracked)
        result.selections.append({final.value(selection.anchor), final.value(selection.active)});
    return result;
}
} // namespace litecode::ui
