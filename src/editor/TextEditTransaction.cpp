#include "editor/TextEditTransaction.h"
#include <algorithm>
#include <limits>

namespace litecode::editor {
namespace {
bool boundary(const QByteArray& text, qsizetype offset) {
    return offset == text.size() || (static_cast<unsigned char>(text.at(offset)) & 0xc0) != 0x80;
}
} // namespace
std::optional<PreparedTextEdits>
prepareTextEdits(const QByteArray& before, QVector<core::DocumentEdit> edits, QString* error) {
    if (error)
        error->clear();
    const auto fail = [error](const char* message) -> std::optional<PreparedTextEdits> {
        if (error)
            *error = QString::fromLatin1(message);
        return {};
    };
    if (QString::fromUtf8(before).toUtf8() != before)
        return fail("Document buffer is not valid UTF-8.");
    for (const auto& edit : edits) {
        if (edit.position < 0 || edit.position > before.size() || edit.removedLength < 0 ||
            edit.removedLength > before.size() - edit.position ||
            QString::fromUtf8(edit.insertedText).toUtf8() != edit.insertedText)
            return fail("Invalid text edit or UTF-8 content.");
        const auto start = static_cast<qsizetype>(edit.position);
        const auto end = start + static_cast<qsizetype>(edit.removedLength);
        if (!boundary(before, start) || !boundary(before, end))
            return fail("Text edit splits a Unicode character.");
        const auto splitsEol = [&before](qsizetype offset) {
            return offset > 0 && offset < before.size() && before.at(offset - 1) == '\r' &&
                   before.at(offset) == '\n';
        };
        if (splitsEol(start) || splitsEol(end))
            return fail("Text edit splits a CRLF line ending.");
    }
    std::stable_sort(edits.begin(), edits.end(), [](const auto& left, const auto& right) {
        if (left.position != right.position)
            return left.position < right.position;
        return left.removedLength < right.removedLength;
    });
    qint64 previousEnd = 0;
    qint64 resultLength = before.size();
    for (const auto& edit : edits) {
        if (edit.position < previousEnd)
            return fail("Overlapping text edits are not permitted.");
        previousEnd = edit.position + edit.removedLength;
        const auto added = edit.insertedText.size();
        if (resultLength - edit.removedLength > std::numeric_limits<qsizetype>::max() - added)
            return fail("Text edit result is too large.");
        resultLength += added - edit.removedLength;
    }
    // Derive UTF-16 event ranges from the original bytes in one forward scan.
    // Callers cannot supply ranges that disagree with their byte coordinates.
    qsizetype cursor = 0;
    core::TextPosition position{};
    const auto advance = [&](qint64 target) {
        while (cursor < target) {
            const auto byte = static_cast<unsigned char>(before.at(cursor));
            if (byte == '\r' || byte == '\n') {
                if (byte == '\r' && cursor + 1 < before.size() && before.at(cursor + 1) == '\n')
                    ++cursor;
                ++cursor;
                ++position.line;
                position.utf16Column = 0;
            } else {
                cursor += byte < 0x80 ? 1 : byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4;
                position.utf16Column += byte < 0xf0 ? 1 : 2;
            }
        }
        return position;
    };
    for (auto& edit : edits) {
        edit.range.start = advance(edit.position);
        edit.range.end = advance(edit.position + edit.removedLength);
    }
    PreparedTextEdits result{before, {}};
    result.edits.reserve(edits.size());
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) {
        if (before.mid(it->position, it->removedLength) == it->insertedText)
            continue;
        result.contents.replace(it->position, it->removedLength, it->insertedText);
        result.edits.append(*it);
    }
    if (result.contents == before)
        result.edits.clear();
    return result;
}
} // namespace litecode::editor
