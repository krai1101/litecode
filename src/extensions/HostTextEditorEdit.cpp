#include "extensions/HostTextEditorEdit.h"
#include <stdexcept>

namespace litecode::extensions {
HostTextEditorEdit::HostTextEditorEdit(std::shared_ptr<HostTextDocument> document, bool before,
                                       bool after)
    : document_(std::move(document)), callbackThread_(std::this_thread::get_id()) {
    data_.documentVersionId = document_->version();
    data_.undoStopBefore = before;
    data_.undoStopAfter = after;
}
TextEditorEditData HostTextEditorEdit::collect(std::shared_ptr<HostTextDocument> document,
                                               const Callback& callback, bool undoStopBefore,
                                               bool undoStopAfter) {
    if (!document)
        throw std::invalid_argument("Text editor document is required.");
    if (!callback)
        throw std::invalid_argument("Text editor edit callback is required.");
    auto builder = std::shared_ptr<HostTextEditorEdit>(
        new HostTextEditorEdit(std::move(document), undoStopBefore, undoStopAfter));
    try {
        callback(builder);
        auto result = builder->finish();
        if (textEditorEditRangesOverlap(result.edits))
            throw std::invalid_argument("Overlapping ranges are not allowed!");
        return result;
    } catch (...) {
        builder->abandon();
        throw;
    }
}
void HostTextEditorEdit::requireActive() const {
    if (!active_ || callbackThread_ != std::this_thread::get_id())
        throw std::logic_error("Edit is only valid while callback runs");
}
void HostTextEditorEdit::push(const Range& range, std::optional<QString> text,
                              bool forceMoveMarkers) {
    std::lock_guard lock(mutex_);
    requireActive();
    data_.edits.append({document_->validateRange(range), std::move(text), forceMoveMarkers});
}
void HostTextEditorEdit::replace(const Position& location, const QString& value) {
    push(Range(location, location), value, false);
}
void HostTextEditorEdit::replace(const Range& location, const QString& value) {
    push(location, value, false);
}
void HostTextEditorEdit::insert(const Position& location, const QString& value) {
    push(Range(location, location), value, true);
}
void HostTextEditorEdit::delete_(const Range& location) { push(location, std::nullopt, true); }
void HostTextEditorEdit::setEndOfLine(EndOfLine endOfLine) {
    std::lock_guard lock(mutex_);
    requireActive();
    if (endOfLine != EndOfLine::LF && endOfLine != EndOfLine::CRLF)
        throw std::invalid_argument("Invalid endOfLine.");
    data_.setEndOfLine = endOfLine;
}
TextEditorEditData HostTextEditorEdit::finish() {
    std::lock_guard lock(mutex_);
    requireActive();
    active_ = false;
    document_.reset();
    return std::move(data_);
}
void HostTextEditorEdit::abandon() noexcept {
    std::lock_guard lock(mutex_);
    active_ = false;
    document_.reset();
    data_.edits.clear();
    data_.setEndOfLine.reset();
}
} // namespace litecode::extensions
