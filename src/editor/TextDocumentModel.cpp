#include "editor/TextDocumentModel.h"
#include "editor/TextEditTransaction.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::editor {
namespace {
bool boundary(const QByteArray& bytes, qsizetype offset) {
    return offset == bytes.size() || (static_cast<unsigned char>(bytes.at(offset)) & 0xc0) != 0x80;
}
bool validUtf8(const QByteArray& bytes) { return QString::fromUtf8(bytes).toUtf8() == bytes; }
} // namespace
TextDocumentModel::TextDocumentModel(core::DocumentId id, QString path, QString languageId)
    : session_(id, std::move(path)), languageId_(std::move(languageId)) {}
bool TextDocumentModel::acceptLoad(const DocumentLoadResult& load) {
    if (load.storageMode == core::DocumentStorageMode::Normal && !validUtf8(load.contents))
        return false;
    auto contents =
        load.storageMode == core::DocumentStorageMode::Normal ? load.contents : QByteArray{};
    if (!session_.acceptLoad(load))
        return false;
    contents_.swap(contents);
    return true;
}
bool TextDocumentModel::initializeUntitled(const QString& content, core::TextEncoding encoding,
                                           core::LineEnding lineEnding) {
    auto initialized = session_.initializeUntitled(content, encoding, lineEnding);
    if (!initialized)
        return false;
    contents_.swap(*initialized);
    return true;
}
bool TextDocumentModel::acceptReload(const DocumentLoadResult& load) {
    if (load.storageMode == core::DocumentStorageMode::Normal && !validUtf8(load.contents))
        return false;
    auto contents =
        load.storageMode == core::DocumentStorageMode::Normal ? load.contents : QByteArray{};
    if (!session_.acceptReload(load))
        return false;
    contents_.swap(contents);
    return true;
}
bool TextDocumentModel::initializeVirtual(const QUrl& resource, const QString& content) {
    if (!resource.isValid() || resource.scheme().isEmpty() || resource.scheme() == "file" ||
        resource.scheme() == "untitled")
        return false;
    auto bytes = session_.initializeVirtual(content);
    if (!bytes)
        return false;
    contents_.swap(*bytes);
    resourceOverride_ = resource;
    return true;
}
void TextDocumentModel::bindVirtualResource(const QUrl& resource) {
    if (!resource.isValid() || resource.scheme().isEmpty() || resource.scheme() == "file" ||
        resource.scheme() == "untitled" || !session_.filePath().isEmpty() ||
        session_.state() != DocumentState::Loading)
        throw std::invalid_argument("Invalid pending virtual document resource.");
    resourceOverride_ = resource;
}
bool TextDocumentModel::applyEdit(core::DocumentEdit* edit) { return applyEditImpl(edit, false); }
bool TextDocumentModel::applyEdits(qint64 expectedVersion, QVector<core::DocumentEdit>* edits,
                                   bool provider, QString* error) {
    if (error)
        error->clear();
    const bool virtualProvider = provider && session_.isReadOnly() &&
                                 resource().scheme() != "file" && resource().scheme() != "untitled";
    if (!edits || !hasTextBuffer() || session_.isClosed() ||
        session_.state() == DocumentState::Loading || session_.version() != expectedVersion ||
        expectedVersion == std::numeric_limits<qint64>::max() ||
        (session_.isReadOnly() && !virtualProvider) || (provider && !virtualProvider)) {
        if (error)
            *error = QStringLiteral("Document changed, is read-only, or is unavailable.");
        return false;
    }
    auto prepared = prepareTextEdits(contents_, *edits, error);
    if (!prepared)
        return false;
    if (prepared->edits.isEmpty()) {
        edits->clear();
        return true;
    }
    // Validation/allocation precede the single state transition. No listener
    // can observe half the transaction or a version per individual operation.
    auto transition = prepared->edits.first();
    if (!session_.applyEdit(&transition))
        return false;
    for (auto& edit : prepared->edits) {
        edit.documentId = session_.id();
        edit.filePath = session_.filePath();
        edit.version = session_.version();
    }
    contents_.swap(prepared->contents);
    edits->swap(prepared->edits);
    if (virtualProvider)
        session_.setModified(false);
    return true;
}
bool TextDocumentModel::applyProviderEdit(core::DocumentEdit* edit) {
    if (!session_.isReadOnly() || resource().scheme() == "file" ||
        resource().scheme() == "untitled")
        return false;
    return applyEditImpl(edit, true);
}
bool TextDocumentModel::applyEditImpl(core::DocumentEdit* edit, bool provider) {
    if (!edit || !hasTextBuffer() || session_.isClosed() ||
        session_.state() == DocumentState::Loading || (session_.isReadOnly() && !provider) ||
        edit->position < 0 || edit->position > contents_.size() || edit->removedLength < 0 ||
        edit->removedLength > contents_.size() - edit->position || !validUtf8(edit->insertedText))
        return false;
    const auto start = static_cast<qsizetype>(edit->position);
    const auto end = start + static_cast<qsizetype>(edit->removedLength);
    if (!boundary(contents_, start) || !boundary(contents_, end))
        return false;
    // Prepare the replacement before advancing state/version. Invalid ranges or
    // allocation failures must not partially mutate a workbench document.
    auto contents = contents_;
    contents.replace(start, end - start, edit->insertedText);
    if (!session_.applyEdit(edit))
        return false;
    contents_.swap(contents);
    if (provider)
        session_.setModified(false);
    return true;
}
bool TextDocumentModel::save(QString* error) {
    if (!hasTextBuffer()) {
        if (error)
            *error = QStringLiteral("The large-file model has no extension text buffer.");
        return false;
    }
    return session_.save(contents_, error);
}
bool TextDocumentModel::replaceWithLineEnding(const QByteArray& text, core::LineEnding eol,
                                              core::DocumentEditReason reason,
                                              std::optional<core::DocumentEdit>* applied,
                                              QString* error) {
    if (applied)
        applied->reset();
    if (error)
        error->clear();
    if (!hasTextBuffer() || session_.isClosed() || session_.isReadOnly() ||
        session_.state() == DocumentState::Loading ||
        session_.version() == std::numeric_limits<qint64>::max() ||
        (eol != core::LineEnding::Lf && eol != core::LineEnding::CrLf) || !validUtf8(text)) {
        if (error)
            *error = QStringLiteral("Invalid EOL edit or unavailable document.");
        return false;
    }
    auto normalized = text;
    normalized.replace("\r\n", "\n");
    normalized.replace('\r', '\n');
    if (eol == core::LineEnding::CrLf)
        normalized.replace("\n", "\r\n");
    const auto previousEol = session_.lineEnding() == core::LineEnding::CrLf
                                 ? core::LineEnding::CrLf
                                 : core::LineEnding::Lf;
    if (normalized == contents_ && previousEol == eol)
        return true;
    const auto before = QString::fromUtf8(contents_);
    const auto lastBreak = before.lastIndexOf(QLatin1Char('\n'));
    core::DocumentEdit edit;
    edit.removedLength = contents_.size();
    edit.insertedText = normalized;
    edit.reason = reason;
    edit.range = {{0, 0},
                  {static_cast<int>(before.count(QLatin1Char('\n'))),
                   static_cast<int>(before.size() - lastBreak - 1)}};
    if (!session_.applyEdit(&edit))
        return false;
    session_.setLineEnding(eol);
    contents_.swap(normalized);
    if (applied)
        *applied = std::move(edit);
    return true;
}
bool TextDocumentModel::saveAs(const QString& path, core::DocumentPathChange* change,
                               QString* error) {
    if (!hasTextBuffer()) {
        if (error)
            *error = QStringLiteral("The large-file model has no extension text buffer.");
        return false;
    }
    return session_.saveAs(path, contents_, change, error);
}
QUrl TextDocumentModel::resource() const {
    return session_.filePath().isEmpty()
               ? (resourceOverride_.isEmpty()
                      ? QUrl(QStringLiteral("untitled:Untitled-%1").arg(session_.id().value()))
                      : resourceOverride_)
               : QUrl::fromLocalFile(session_.filePath());
}
void TextDocumentModel::setUntitledResource(QUrl uri) {
    if (!session_.filePath().isEmpty() || uri.scheme() != QStringLiteral("untitled") ||
        !uri.isValid())
        throw std::invalid_argument("Invalid untitled resource.");
    resourceOverride_ = std::move(uri);
}
} // namespace litecode::editor
