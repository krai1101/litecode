#include "extensions/WorkbenchDocuments.h"

namespace litecode::extensions {
void WorkbenchDocuments::update(TextDocumentSnapshot snapshot) {
    const auto id = snapshot.id;
    DocumentTextBuffer buffer(snapshot.text, snapshot.eol);
    snapshot.text = buffer.text();
    const auto previous = documents_.constFind(id);
    if (previous != documents_.cend() && previous->metadata.uri == snapshot.uri &&
        previous->metadata.version == snapshot.version &&
        previous->metadata.dirty == snapshot.dirty &&
        previous->metadata.languageId == snapshot.languageId &&
        previous->metadata.encoding == snapshot.encoding &&
        previous->metadata.eol == snapshot.eol && previous->buffer.text() == snapshot.text)
        return;
    if (previous != documents_.cend() && previous->metadata.uri != snapshot.uri)
        broadcast_({{"command", "documentClose"}, {"id", id}});
    auto metadata = snapshot;
    metadata.text.clear();
    metadata.reason.reset();
    documents_.insert(id, Document{std::move(metadata), std::move(buffer)});
    wire::sendDocumentSnapshot(snapshot, broadcast_);
}
bool WorkbenchDocuments::metadata(const TextDocumentMetadata& metadata) {
    auto it = documents_.find(metadata.id);
    if (it == documents_.end())
        return false;
    auto& current = it->metadata;
    if (current.dirty == metadata.dirty && current.encoding == metadata.encoding &&
        current.languageId == metadata.languageId)
        return true;
    current.dirty = metadata.dirty;
    current.encoding = metadata.encoding;
    current.languageId = metadata.languageId;
    broadcast_(wire::encodeDocumentMetadata(metadata));
    return true;
}
bool WorkbenchDocuments::change(const QString& id, qint64 version, const Range& range,
                                const QString& text, bool dirty,
                                std::optional<TextDocumentChangeReason> reason) {
    auto it = documents_.find(id);
    if (it == documents_.end() || version <= it->metadata.version)
        return false;
    const auto previous = it->metadata.version;
    TextDocumentContentChangeEvent applied{Range(0, 0, 0, 0), 0, 0, {}};
    try {
        applied = it->buffer.apply(range, text);
    } catch (const std::exception&) {
        return false;
    }
    it->metadata.version = version;
    it->metadata.dirty = dirty;
    wire::sendDocumentChange({id, previous, version, range, applied.text, dirty, reason},
                             broadcast_);
    return true;
}
void WorkbenchDocuments::close(const QString& id) {
    if (documents_.remove(id))
        broadcast_({{"command", "documentClose"}, {"id", id}});
}
bool WorkbenchDocuments::changeBatch(const QString& id, qint64 version,
                                     const QVector<TextEdit>& edits, bool dirty,
                                     std::optional<TextDocumentChangeReason> reason) {
    auto it = documents_.find(id);
    if (it == documents_.end() || version <= it->metadata.version || edits.isEmpty() ||
        edits.size() > 100000)
        return false;
    qint64 length = 0;
    for (const auto& edit : edits) {
        length += edit.newText.size();
        if (length > 50 * 1024 * 1024)
            return false;
    }
    const auto previous = it->metadata.version;
    try {
        const auto changes = it->buffer.applyBatch(edits);
        Q_UNUSED(changes);
    } catch (const std::exception&) {
        return false;
    }
    it->metadata.version = version;
    it->metadata.dirty = dirty;
    wire::sendDocumentBatch({id, previous, version, edits, dirty, reason}, broadcast_);
    return true;
}
void WorkbenchDocuments::saved(const QString& id) {
    if (documents_.contains(id))
        broadcast_({{"command", "documentSaved"}, {"id", id}});
}
void WorkbenchDocuments::sendInitial(const std::function<void(const QJsonObject&)>& send) const {
    auto ids = documents_.keys();
    ids.sort();
    for (const auto& id : ids) {
        const auto document = documents_.constFind(id);
        auto snapshot = document->metadata;
        snapshot.text = document->buffer.text();
        wire::sendDocumentSnapshot(snapshot, send);
    }
}
bool WorkbenchDocuments::save(const QString& id, QString* error) {
    if (error)
        error->clear();
    if (!documents_.contains(id)) {
        if (error)
            *error = "Document has been closed.";
        return false;
    }
    if (!save_) {
        if (error)
            *error = "Workbench document save service is unavailable.";
        return false;
    }
    // The delegate synchronizes metadata/text through update() before returning.
    const bool saved = save_(id, error);
    // Ordinary save failure is false, not a rejected TextDocument.save future.
    // Missing/closed documents and service failures above remain exceptional.
    if (!saved && error)
        error->clear();
    return saved;
}
std::optional<QUrl> WorkbenchDocuments::saveResource(const QUrl& uri, bool saveAs, QString* error) {
    if (error)
        error->clear();
    if (!workspaceSave_) {
        if (error)
            *error = "Workspace save service unavailable.";
        return {};
    }
    const auto delegate = workspaceSave_;
    return delegate(uri, saveAs, error);
}
bool WorkbenchDocuments::saveAll(bool includeUntitled, QString* error) {
    if (error)
        error->clear();
    if (!saveAll_) {
        if (error)
            *error = "Workspace save-all service unavailable.";
        return false;
    }
    const auto delegate = saveAll_;
    return delegate(includeUntitled, error);
}
} // namespace litecode::extensions
