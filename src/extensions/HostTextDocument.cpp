#include "extensions/HostTextDocument.h"
#include <QMutexLocker>
#include <QPromise>
#include <QDir>
#include <algorithm>

namespace litecode::extensions {
namespace {
QRegularExpression defaultWordDefinition() {
    const auto separators = QStringLiteral("`~!@#$%^&*()-=+[{]}\\|;:'\",.<>/?");
    return QRegularExpression(QStringLiteral("(-?\\d*\\.\\d\\w*)|([^") +
        QRegularExpression::escape(separators) + QStringLiteral("\\s]+)"));
}
QFuture<bool> failedSave(const char* message) {
    QPromise<bool> promise;
    promise.start();
    auto future = promise.future();
    promise.setException(std::make_exception_ptr(std::runtime_error(message)));
    promise.finish();
    return future;
}
} // namespace
HostTextDocument::HostTextDocument(TextDocumentSnapshot snapshot, std::function<QFuture<bool>(const QString&)> save)
    : state_(std::move(snapshot)), save_(std::move(save)) { rebuildLines(); }
void HostTextDocument::rebuildLines() {
    buffer_ = DocumentTextBuffer(std::move(state_.text), state_.eol);
    state_.text.clear();
}
QUrl HostTextDocument::uri() const { QMutexLocker lock(&mutex_); return state_.uri; }
QString HostTextDocument::fileName() const {
    QMutexLocker lock(&mutex_);
    return QDir::toNativeSeparators(state_.uri.isLocalFile() ? state_.uri.toLocalFile() : state_.uri.path());
}
bool HostTextDocument::isUntitled() const { QMutexLocker lock(&mutex_); return state_.uri.scheme() == "untitled"; }
QString HostTextDocument::languageId() const { QMutexLocker lock(&mutex_); return state_.languageId; }
QString HostTextDocument::encoding() const { QMutexLocker lock(&mutex_); return state_.encoding; }
qint64 HostTextDocument::version() const { QMutexLocker lock(&mutex_); return state_.version; }
bool HostTextDocument::isDirty() const { QMutexLocker lock(&mutex_); return state_.dirty; }
bool HostTextDocument::isClosed() const { QMutexLocker lock(&mutex_); return closed_; }
EndOfLine HostTextDocument::eol() const { QMutexLocker lock(&mutex_); return state_.eol; }
int HostTextDocument::lineCount() const { QMutexLocker lock(&mutex_); return buffer_.lineCount(); }
QFuture<bool> HostTextDocument::save() {
    std::function<QFuture<bool>(const QString&)> delegate;
    QString id;
    { QMutexLocker lock(&mutex_); if (closed_) return failedSave("Document has been closed."); delegate = save_; id = state_.id; }
    if (!delegate) return failedSave("Document save service is unavailable.");
    try { return delegate(id); } catch (...) {
        QPromise<bool> promise;
        promise.start();
        auto future = promise.future();
        promise.setException(std::current_exception());
        promise.finish();
        return future;
    }
}
TextLine HostTextDocument::lineAt(int line) const {
    QMutexLocker lock(&mutex_);
    return TextLine(line, buffer_.line(line), line == buffer_.lineCount() - 1);
}
TextLine HostTextDocument::lineAt(const Position& position) const {
    // The upstream implementation rejects out-of-range lines despite the
    // declaration's adjustment note; retain that implementation behavior.
    return lineAt(position.line());
}
Position HostTextDocument::validatePosition(const Position& position) const {
    QMutexLocker lock(&mutex_);
    return buffer_.validate(position);
}
Range HostTextDocument::validateRange(const Range& range) const {
    QMutexLocker lock(&mutex_);
    return Range(validatePosition(range.start()), validatePosition(range.end()));
}
qint64 HostTextDocument::offsetAt(const Position& position) const {
    QMutexLocker lock(&mutex_);
    return buffer_.offsetAt(position);
}
Position HostTextDocument::positionAt(qint64 offset) const {
    QMutexLocker lock(&mutex_);
    return buffer_.positionAt(offset);
}
QString HostTextDocument::getText(const std::optional<Range>& range) const {
    QMutexLocker lock(&mutex_);
    return range ? buffer_.text(*range) : buffer_.text();
}
std::optional<Range> HostTextDocument::getWordRangeAtPosition(const Position& position,
    const std::optional<QRegularExpression>& regex) const {
    QString text;
    Position valid(0, 0);
    { QMutexLocker lock(&mutex_); valid = validatePosition(position); text = buffer_.line(valid.line()); }
    const auto expression = regex.value_or(defaultWordDefinition());
    if (!expression.isValid()) throw std::invalid_argument("Invalid word regular expression.");
    if (expression.match(QString{}).hasMatch()) throw std::invalid_argument("Word regular expression matches an empty string.");
    // Match only a bounded window around the requested UTF-16 offset, as
    // upstream does, rather than scanning arbitrarily long source lines.
    const int start = qMax(0, valid.character() - 500);
    const auto window = text.mid(start, 1000);
    auto matches = expression.globalMatch(window);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedLength() == 0) throw std::invalid_argument("Word regular expression matches an empty string.");
        const auto from = start + match.capturedStart(), to = start + match.capturedEnd();
        if (from <= valid.character() && to >= valid.character())
            return Range(valid.line(), static_cast<int>(from), valid.line(), static_cast<int>(to));
        if (from > valid.character()) break;
    }
    return std::nullopt;
}
void HostTextDocument::accept(TextDocumentSnapshot snapshot) {
    // Build and normalize the replacement before committing it. An invalid
    // or oversized update must leave the existing readable mirror intact.
    HostTextDocument replacement(std::move(snapshot), {});
    QMutexLocker lock(&mutex_);
    const auto& next = replacement.state_;
    if (closed_ || next.id != state_.id || next.uri != state_.uri || next.version < state_.version)
        throw std::invalid_argument("Invalid document mirror update.");
    if (next.version == state_.version && (replacement.buffer_.text() != buffer_.text() || next.eol != state_.eol))
        throw std::invalid_argument("Document text changed without advancing its version.");
    state_ = std::move(replacement.state_);
    buffer_ = std::move(replacement.buffer_);
}
TextDocumentContentChangeEvent HostTextDocument::change(qint64 previousVersion, qint64 version, const Range& range, const QString& text, bool dirty) {
    QMutexLocker lock(&mutex_);
    if (closed_ || previousVersion != state_.version || version <= previousVersion)
        throw std::invalid_argument("Document edit version is out of order.");
    auto event = buffer_.apply(range, text);
    state_.version = version;
    state_.dirty = dirty;
    return event;
}
void HostTextDocument::metadata(const TextDocumentMetadata& metadata) {
    QMutexLocker lock(&mutex_);
    if (closed_ || metadata.id != state_.id) throw std::invalid_argument("Invalid document metadata update.");
    state_.languageId = metadata.languageId;
    state_.encoding = metadata.encoding;
    state_.dirty = metadata.dirty;
}
void HostTextDocument::close() {
    QMutexLocker lock(&mutex_);
    closed_ = true;
    state_.dirty = false;
    save_ = {};
}
QVector<TextDocumentContentChangeEvent> HostTextDocument::changeBatch(const wire::DocumentBatch& batch) {
    QMutexLocker lock(&mutex_);
    if (closed_ || batch.id != state_.id || batch.previousVersion != state_.version || batch.version <= state_.version)
        throw std::invalid_argument("Document batch version is out of order.");
    auto changes = buffer_.applyBatch(batch.edits);
    state_.version = batch.version;
    state_.dirty = batch.dirty;
    return changes;
}
} // namespace litecode::extensions
