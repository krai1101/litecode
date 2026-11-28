#include "extensions/HostDocuments.h"
#include <QMutexLocker>
#include <QThread>
#include <algorithm>

namespace litecode::extensions {
HostDocuments::HostDocuments(std::function<QFuture<bool>(const QString&)> save, QObject* parent)
    : QObject(parent), save_(std::move(save)) {}
HostDocuments::~HostDocuments() { shutdown(); }
std::shared_ptr<TextDocument> HostDocuments::find(const QString& id) const {
    QMutexLocker lock(&mutex_);
    return documents_.value(id);
}
QVector<std::shared_ptr<TextDocument>> HostDocuments::textDocuments() const {
    QMutexLocker lock(&mutex_);
    auto ids = documents_.keys();
    ids.sort();
    QVector<std::shared_ptr<TextDocument>> result;
    for (const auto& id : ids)
        result.append(documents_.value(id));
    return result;
}
void HostDocuments::accept(TextDocumentSnapshot snapshot, bool initial) {
    if (QThread::currentThread() != thread())
        throw std::logic_error("Document updates must run on the host dispatcher.");
    std::shared_ptr<HostTextDocument> document;
    std::optional<TextDocumentChangeEvent> change;
    bool created = false;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            throw std::logic_error("Document service has stopped.");
        if (snapshot.id.isEmpty() || !snapshot.uri.isValid() || snapshot.uri.isEmpty() ||
            snapshot.version < 1 ||
            (snapshot.eol != EndOfLine::LF && snapshot.eol != EndOfLine::CRLF))
            throw std::invalid_argument("Invalid document snapshot.");
        document = documents_.value(snapshot.id);
        if (!document) {
            document = std::make_shared<HostTextDocument>(snapshot, save_);
            documents_.insert(snapshot.id, document);
            created = true;
        } else {
            const auto oldText = document->getText();
            const auto oldEol = document->eol();
            const auto oldRange = Range(Position(0, 0), document->positionAt(oldText.size()));
            const bool metadataChanged =
                document->encoding() != snapshot.encoding || document->isDirty() != snapshot.dirty;
            const auto reason = snapshot.reason;
            document->accept(std::move(snapshot));
            const auto newText = document->getText();
            const bool eolChanged = oldEol != document->eol();
            if (oldText != newText || metadataChanged || eolChanged) {
                change = TextDocumentChangeEvent{document, {}, reason};
                if (oldText != newText || eolChanged)
                    change->contentChanges.append({oldRange, 0, oldText.size(), newText});
            }
        }
    }
    // Never invoke extension callbacks while holding a service or mirror lock.
    if (!initial && created)
        opened_.fire(document);
    if (!initial && change)
        changed_.fire(*change);
}
void HostDocuments::close(const QString& id) {
    std::shared_ptr<HostTextDocument> document;
    {
        QMutexLocker lock(&mutex_);
        document = documents_.take(id);
    }
    if (!document)
        return;
    document->close();
    closed_.fire(document);
}
void HostDocuments::change(const QString& id, qint64 previousVersion, qint64 version,
                           const Range& range, const QString& text, bool dirty,
                           std::optional<TextDocumentChangeReason> reason) {
    if (QThread::currentThread() != thread())
        throw std::logic_error("Document updates must run on the host dispatcher.");
    std::shared_ptr<HostTextDocument> document;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            throw std::logic_error("Document service has stopped.");
        document = documents_.value(id);
    }
    if (!document)
        throw std::invalid_argument("Document edit refers to an unknown mirror.");
    const auto event = document->change(previousVersion, version, range, text, dirty);
    changed_.fire({document, {event}, reason});
}
void HostDocuments::saved(const QString& id) {
    std::shared_ptr<HostTextDocument> document;
    {
        QMutexLocker lock(&mutex_);
        document = documents_.value(id);
    }
    if (document)
        saved_.fire(document);
}
void HostDocuments::changeBatch(const wire::DocumentBatch& batch) {
    if (QThread::currentThread() != thread())
        throw std::logic_error("Document updates must run on the host dispatcher.");
    std::shared_ptr<HostTextDocument> document;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            throw std::logic_error("Document service has stopped.");
        document = documents_.value(batch.id);
    }
    if (!document)
        throw std::invalid_argument("Document batch refers to an unknown mirror.");
    const auto changes = document->changeBatch(batch);
    changed_.fire({document, changes, batch.reason});
}
void HostDocuments::metadata(const TextDocumentMetadata& metadata) {
    if (QThread::currentThread() != thread())
        throw std::logic_error("Document updates must run on the host dispatcher.");
    std::shared_ptr<HostTextDocument> document;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            throw std::logic_error("Document service has stopped.");
        document = documents_.value(metadata.id);
    }
    if (!document)
        throw std::invalid_argument("Metadata refers to an unknown document.");
    const bool languageChanged = document->languageId() != metadata.languageId;
    const bool changed =
        document->encoding() != metadata.encoding || document->isDirty() != metadata.dirty;
    // Upstream treats a language change as remove/add on the same live object.
    // It does not close that object or erase its text.
    if (languageChanged)
        closed_.fire(document);
    document->metadata(metadata);
    if (languageChanged)
        opened_.fire(document);
    if (changed)
        changed_.fire({document, {}, {}});
}
void HostDocuments::shutdown() {
    QHash<QString, std::shared_ptr<HostTextDocument>> documents;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        documents = std::exchange(documents_, {});
        save_ = {};
    }
    opened_.dispose();
    changed_.dispose();
    closed_.dispose();
    saved_.dispose();
    for (const auto& document : documents)
        document->close();
}
} // namespace litecode::extensions
