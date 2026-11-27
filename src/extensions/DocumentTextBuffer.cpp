#include "extensions/DocumentTextBuffer.h"
#include <limits>

namespace litecode::extensions {
QVector<TextDocumentContentChangeEvent> DocumentTextBuffer::applyBatch(const QVector<TextEdit>& edits) {
    std::optional<qint64> previousStart;
    for (const auto& edit : edits) {
        if (edit.newEol || !validate(edit.range.start()).isEqual(edit.range.start()) ||
            !validate(edit.range.end()).isEqual(edit.range.end()))
            throw std::invalid_argument("Invalid document batch range.");
        const auto start = offsetAt(edit.range.start()), end = offsetAt(edit.range.end());
        if (previousStart && end > *previousStart) throw std::invalid_argument("Overlapping or unordered document batch.");
        previousStart = start;
    }
    auto next = *this;
    QVector<TextDocumentContentChangeEvent> changes;
    changes.reserve(edits.size());
    for (const auto& edit : edits) changes.append(next.apply(edit.range, edit.newText));
    *this = std::move(next);
    return changes;
}
DocumentTextBuffer::DocumentTextBuffer(QString text, EndOfLine eol) : eol_(eol) {
    if ((eol != EndOfLine::LF && eol != EndOfLine::CRLF) || text.size() > std::numeric_limits<int>::max())
        throw std::invalid_argument("Invalid document text buffer.");
    lines_ = text.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")), Qt::KeepEmptyParts);
    rebuild();
}
QString DocumentTextBuffer::separator() const { return eol_ == EndOfLine::CRLF ? QStringLiteral("\r\n") : QStringLiteral("\n"); }
const QString& DocumentTextBuffer::line(int index) const {
    if (index < 0 || index >= lines_.size()) throw std::out_of_range("Illegal document line.");
    return lines_.at(index);
}
void DocumentTextBuffer::add(int index, qint64 delta) {
    for (qint64 i = qint64(index) + 1; i < sums_.size(); i += i & -i) sums_[i] += delta;
}
void DocumentTextBuffer::rebuild() {
    if (lines_.size() > std::numeric_limits<int>::max())
        throw std::length_error("Document exceeds native line limits.");
    sums_.fill(0, lines_.size() + 1);
    for (qint64 i = 1; i < sums_.size(); ++i) {
        sums_[i] += lines_.at(i - 1).size() + separator().size();
        const auto parent = i + (i & -i);
        if (parent < sums_.size()) sums_[parent] += sums_.at(i);
    }
    if (prefix(lineCount()) - separator().size() > std::numeric_limits<int>::max())
        throw std::length_error("Document exceeds native coordinate limits.");
    cachedText_.reset();
}
qint64 DocumentTextBuffer::prefix(int count) const {
    qint64 result = 0;
    for (int i = count; i > 0; i -= i & -i) result += sums_.at(i);
    return result;
}
Position DocumentTextBuffer::validate(const Position& position) const {
    if (position.line() >= lineCount()) return Position(lineCount() - 1, static_cast<int>(lines_.last().size()));
    return Position(position.line(), qMin(position.character(), static_cast<int>(lines_.at(position.line()).size())));
}
qint64 DocumentTextBuffer::offsetAt(const Position& position) const {
    const auto valid = validate(position);
    return prefix(valid.line()) + valid.character();
}
Position DocumentTextBuffer::positionAt(qint64 offset) const {
    offset = qBound<qint64>(0, offset, prefix(lineCount()) - separator().size());
    int index = 0, step = 1;
    while (step <= lineCount() / 2) step <<= 1;
    qint64 consumed = 0;
    for (; step; step >>= 1) {
        const auto next = qint64(index) + step;
        if (next < sums_.size() && consumed + sums_.at(next) <= offset) { index = static_cast<int>(next); consumed += sums_.at(next); }
    }
    index = qMin(index, lineCount() - 1);
    return Position(index, static_cast<int>(qMin<qint64>(offset - prefix(index), lines_.at(index).size())));
}
QString DocumentTextBuffer::text() const {
    if (!cachedText_) cachedText_ = lines_.join(separator());
    return *cachedText_;
}
QString DocumentTextBuffer::text(const Range& range) const {
    const auto start = validate(range.start()), end = validate(range.end());
    if (start.line() == end.line()) return lines_.at(start.line()).mid(start.character(), end.character() - start.character());
    QStringList selected;
    selected.append(lines_.at(start.line()).mid(start.character()));
    for (int i = start.line() + 1; i < end.line(); ++i) selected.append(lines_.at(i));
    selected.append(lines_.at(end.line()).left(end.character()));
    return selected.join(separator());
}
TextDocumentContentChangeEvent DocumentTextBuffer::apply(const Range& range, const QString& text) {
    if (!validate(range.start()).isEqual(range.start()) || !validate(range.end()).isEqual(range.end()))
        throw std::invalid_argument("Document edit range is outside the mirror.");
    auto inserted = text.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")), Qt::KeepEmptyParts);
    const auto normalized = inserted.join(separator());
    const auto start = offsetAt(range.start()), end = offsetAt(range.end());
    if (prefix(lineCount()) - separator().size() - (end - start) + normalized.size() > std::numeric_limits<int>::max())
        throw std::length_error("Document edit exceeds native coordinate limits.");
    const auto first = range.start().line(), last = range.end().line();
    inserted.first().prepend(lines_.at(first).left(range.start().character()));
    inserted.last().append(lines_.at(last).mid(range.end().character()));
    if (first == last && inserted.size() == 1) {
        const auto delta = inserted.first().size() - lines_.at(first).size();
        // Complete any copy-on-write allocations before changing either the
        // text or its index, so an allocation failure cannot split their state.
        lines_.detach();
        sums_.detach();
        lines_[first] = std::move(inserted.first());
        add(first, delta);
        cachedText_.reset();
    } else {
        auto replacement = lines_.mid(0, first);
        replacement.append(inserted);
        replacement.append(lines_.mid(last + 1));
        DocumentTextBuffer next({}, eol_);
        next.lines_ = std::move(replacement);
        next.rebuild();
        *this = std::move(next);
    }
    return {range, start, end - start, normalized};
}
} // namespace litecode::extensions
