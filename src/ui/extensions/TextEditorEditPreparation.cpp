#include "ui/extensions/TextEditorEditPreparation.h"
#include <QHash>
#include <algorithm>
#include <limits>

namespace litecode::ui {
namespace {
class Coordinates final {
  public:
    explicit Coordinates(QString text) : text_(std::move(text)) {
        qsizetype start = 0;
        for (qsizetype index = 0; index < text_.size(); ++index) {
            if (text_[index] != '\r' && text_[index] != '\n')
                continue;
            lines_.append({start, index});
            if (text_[index] == '\r' && index + 1 < text_.size() && text_[index + 1] == '\n')
                ++index;
            start = index + 1;
        }
        lines_.append({start, text_.size()});
    }
    QPair<qsizetype, qsizetype> range(const extensions::Range& range) const {
        auto start = offset(range.start()), end = offset(range.end());
        const bool startInside = insideSurrogate(start), endInside = insideSurrogate(end);
        if (start == end && startInside)
            return {start - 1, end - 1};
        if (startInside)
            --start;
        if (endInside)
            ++end;
        return {start, end};
    }
    bool isLineEnd(qsizetype offset) const {
        const auto line = std::lower_bound(
            lines_.begin(), lines_.end(), offset,
            [](const auto& bounds, qsizetype value) { return bounds.second < value; });
        return line != lines_.end() && line->second == offset;
    }
    QHash<qsizetype, qint64> byteOffsets(QVector<qsizetype> offsets) const {
        std::sort(offsets.begin(), offsets.end());
        offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
        QHash<qsizetype, qint64> result;
        result.reserve(offsets.size());
        qsizetype cursor = 0;
        qint64 bytes = 0;
        for (const auto target : offsets) {
            while (cursor < target) {
                const auto character = text_[cursor++];
                if (character.isHighSurrogate()) {
                    ++cursor;
                    bytes += 4;
                } else
                    bytes += character.unicode() < 0x80 ? 1 : character.unicode() < 0x800 ? 2 : 3;
            }
            result.insert(target, bytes);
        }
        return result;
    }

  private:
    qsizetype offset(const extensions::Position& position) const {
        if (position.line() >= lines_.size())
            return text_.size();
        const auto line = lines_[position.line()];
        return line.first + qMin<qsizetype>(position.character(), line.second - line.first);
    }
    bool insideSurrogate(qsizetype offset) const {
        return offset > 0 && offset < text_.size() && text_[offset - 1].isHighSurrogate() &&
               text_[offset].isLowSurrogate();
    }
    QString text_;
    QVector<QPair<qsizetype, qsizetype>> lines_;
};
} // namespace
std::optional<PreparedTextEditorEdit>
prepareTextEditorEdit(const editor::TextDocumentModel& model,
                      const extensions::TextEditorEditData& data, QString* error) {
    if (error)
        error->clear();
    const auto fail = [&](const char* message) -> std::optional<PreparedTextEditorEdit> {
        if (error)
            *error = QString::fromLatin1(message);
        return {};
    };
    const auto& session = model.session();
    if (!model.hasTextBuffer() || session.isClosed() || session.isReadOnly() ||
        session.state() == editor::DocumentState::Loading ||
        session.version() >= std::numeric_limits<qint64>::max() - 1 ||
        data.documentVersionId != session.version() + 1)
        return fail("Editor document changed, is read-only or is unavailable.");
    if (data.setEndOfLine && *data.setEndOfLine != extensions::EndOfLine::LF &&
        *data.setEndOfLine != extensions::EndOfLine::CRLF)
        return fail("Invalid editor end-of-line value.");
    if (extensions::textEditorEditRangesOverlap(data.edits))
        return fail("Overlapping editor edits.");
    PreparedTextEditorEdit result{model, {}, {}, {}, data.undoStopBefore, data.undoStopAfter};
    // mainThreadEditor.applyEdits pushes EOL before executing text edits.
    // Consequently both byte coordinates and inserted newline normalization
    // below must use the resulting EOL, not the original snapshot's EOL.
    if (data.setEndOfLine) {
        const auto eol = *data.setEndOfLine == extensions::EndOfLine::CRLF ? core::LineEnding::CrLf
                                                                           : core::LineEnding::Lf;
        if (!result.replacement.replaceWithLineEnding(result.replacement.contents(), eol,
                                                      core::DocumentEditReason::Other,
                                                      &result.eolEdit, error))
            return {};
    }
    const auto& bytes = result.replacement.contents();
    const auto text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes)
        return fail("Editor document is not valid UTF-8.");
    Coordinates coordinates(text);
    QVector<QPair<qsizetype, qsizetype>> ranges;
    QVector<qsizetype> endpoints;
    ranges.reserve(data.edits.size());
    for (const auto& operation : data.edits) {
        const auto range = coordinates.range(operation.range);
        ranges.append(range);
        endpoints.append(range.first);
        endpoints.append(range.second);
    }
    const auto offsets = coordinates.byteOffsets(std::move(endpoints));
    QVector<core::DocumentEdit> modelEdits;
    modelEdits.reserve(data.edits.size());
    result.operations.reserve(data.edits.size());
    for (qsizetype index = 0; index < data.edits.size(); ++index) {
        const auto& operation = data.edits[index];
        const auto range = ranges[index];
        auto insertion = operation.text.value_or(QString{});
        if (QString::fromUtf8(insertion.toUtf8()) != insertion)
            return fail("Editor insertion contains an unpaired Unicode surrogate.");
        // textModel._validateEditOperation removes a trailing lone CR at a
        // validated line end in CRLF models before normalizing newlines. Without
        // this step it would introduce an extra line rather than combine with
        // the existing CRLF. The upstream rule also applies at the final line.
        if (result.replacement.session().lineEnding() == core::LineEnding::CrLf &&
            insertion.endsWith(QLatin1Char('\r')) && coordinates.isLineEnd(range.second))
            insertion.chop(1);
        insertion.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        insertion.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        if (result.replacement.session().lineEnding() == core::LineEnding::CrLf)
            insertion.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
        core::DocumentEdit converted;
        converted.position = offsets.value(range.first);
        converted.removedLength = offsets.value(range.second) - converted.position;
        converted.insertedText = insertion.toUtf8();
        modelEdits.append(converted);
        result.operations.append({std::move(converted), operation.forceMoveMarkers, range.first,
                                  range.second - range.first, insertion.size()});
    }
    // Shared model preparation rechecks overlap after surrogate adjustment.
    if (!result.replacement.applyEdits(result.replacement.session().version(), &modelEdits, false,
                                       error))
        return {};
    if (result.replacement.session().version() == std::numeric_limits<qint64>::max())
        return fail("Editor document version space is exhausted.");
    result.contentChanges = std::move(modelEdits);
    std::stable_sort(result.operations.begin(), result.operations.end(),
                     [](const auto& left, const auto& right) {
                         if (left.edit.position != right.edit.position)
                             return left.edit.position < right.edit.position;
                         return left.edit.removedLength < right.edit.removedLength;
                     });
    std::reverse(result.operations.begin(), result.operations.end());
    return result;
}
} // namespace litecode::ui
