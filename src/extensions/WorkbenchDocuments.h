#pragma once
#include "extensions/DocumentOpenApi.h"
#include "extensions/DocumentTextBuffer.h"
#include "extensions/DocumentWire.h"
#include "extensions/WorkspaceEditWire.h"
#include <QFuture>
#include <QHash>
#include <stdexcept>

namespace litecode::extensions {
// Workbench-owned mirror source and save delegate. It never reads a file behind
// the editor's back; snapshots come from the live document model.
class WorkbenchDocuments final {
  public:
    explicit WorkbenchDocuments(std::function<void(const QJsonObject&)> broadcast)
        : broadcast_(std::move(broadcast)) {}
    void update(TextDocumentSnapshot snapshot);
    bool metadata(const TextDocumentMetadata& metadata);
    bool change(const QString& id, qint64 version, const Range& range, const QString& text,
                bool dirty, std::optional<TextDocumentChangeReason> reason = {});
    void close(const QString& id);
    bool changeBatch(const QString& id, qint64 version, const QVector<TextEdit>& edits, bool dirty,
                     std::optional<TextDocumentChangeReason> reason = {});
    void saved(const QString& id);
    void sendInitial(const std::function<void(const QJsonObject&)>& send) const;
    void setSaveDelegate(std::function<bool(const QString&, QString*)> save) {
        save_ = std::move(save);
    }
    bool save(const QString& id, QString* error);
    void
    setWorkspaceSaveDelegate(std::function<std::optional<QUrl>(const QUrl&, bool, QString*)> save,
                             std::function<bool(bool, QString*)> saveAll) {
        workspaceSave_ = std::move(save);
        saveAll_ = std::move(saveAll);
    }
    std::optional<QUrl> saveResource(const QUrl& uri, bool saveAs, QString* error);
    bool saveAll(bool includeUntitled, QString* error);
    void
    setOpenDelegate(std::function<QFuture<TextDocumentSnapshot>(const QString&, const QUrl&,
                                                                const TextDocumentOpenOptions&)>
                        open,
                    std::function<QFuture<TextDocumentSnapshot>(const QString&,
                                                                const UntitledTextDocumentOptions&)>
                        create,
                    std::function<void(const QString&)> removeOwner = {}) {
        open_ = std::move(open);
        create_ = std::move(create);
        removeOwner_ = std::move(removeOwner);
    }
    QFuture<TextDocumentSnapshot> open(const QString& owner, const QUrl& uri,
                                       const TextDocumentOpenOptions& options) {
        if (!open_)
            throw std::runtime_error("Workbench document opening service unavailable.");
        return open_(owner, uri, options);
    }
    QFuture<TextDocumentSnapshot> create(const QString& owner,
                                         const UntitledTextDocumentOptions& options) {
        if (!create_)
            throw std::runtime_error("Workbench document opening service unavailable.");
        return create_(owner, options);
    }
    void setApplyEditDelegate(
        std::function<QFuture<bool>(const QString&, const WorkspaceEditRequest&)> apply,
        std::function<void(const QString&)> removeOwner) {
        applyEdit_ = std::move(apply);
        removeEditOwner_ = std::move(removeOwner);
    }
    QFuture<bool> applyEdit(const QString& owner, const WorkspaceEditRequest& request) {
        if (!applyEdit_)
            throw std::runtime_error("Workspace edit service unavailable.");
        return applyEdit_(owner, request);
    }
    void removeOwner(const QString& owner) {
        if (removeEditOwner_)
            removeEditOwner_(owner);
        if (removeOwner_)
            removeOwner_(owner);
    }
    [[nodiscard]] bool contains(const QString& id, const QUrl& resource) const {
        const auto document = documents_.constFind(id);
        return document != documents_.cend() && document->metadata.uri == resource;
    }

  private:
    struct Document {
        TextDocumentSnapshot metadata;
        DocumentTextBuffer buffer;
    };
    QHash<QString, Document> documents_;
    std::function<void(const QJsonObject&)> broadcast_;
    std::function<bool(const QString&, QString*)> save_;
    std::function<std::optional<QUrl>(const QUrl&, bool, QString*)> workspaceSave_;
    std::function<bool(bool, QString*)> saveAll_;
    std::function<QFuture<TextDocumentSnapshot>(const QString&, const QUrl&,
                                                const TextDocumentOpenOptions&)>
        open_;
    std::function<QFuture<TextDocumentSnapshot>(const QString&, const UntitledTextDocumentOptions&)>
        create_;
    std::function<void(const QString&)> removeOwner_;
    std::function<QFuture<bool>(const QString&, const WorkspaceEditRequest&)> applyEdit_;
    std::function<void(const QString&)> removeEditOwner_;
};
} // namespace litecode::extensions
