#pragma once
#include "extensions/HostConfiguration.h"
#include "extensions/LocalFileSystem.h"
#include "extensions/HostDocuments.h"
#include "extensions/HostWorkspaceRequests.h"
#include "extensions/HostDocumentOpening.h"
#include "extensions/HostContentProviders.h"
#include <QPointer>
#include <stdexcept>

namespace litecode::extensions {
// Composition root for workspace services. Backend ownership stays separate
// from configuration resolution and the plugin-facing abstract contract.
class HostWorkspace final : public Workspace {
  public:
    HostWorkspace(HostConfiguration& configuration, HostDocuments& documents, HostWorkspaceRequests& requests,
        HostContentProviders& contentProviders)
        : configuration_(configuration), documents_(documents), requests_(requests), opening_(documents, requests), contentProviders_(contentProviders) {}
    void shutdown() { opening_.shutdown(); contentProviders_.shutdown(); }
    Disposable registerTextDocumentContentProvider(const QString& scheme, std::shared_ptr<TextDocumentContentProvider> provider) override {
        return contentProviders_.registerProvider(scheme, std::move(provider));
    }
    FileSystem& fs() override { return files_; }
    QFuture<bool> applyEdit(const WorkspaceEdit& edit, const std::optional<WorkspaceEditMetadata>& metadata = {}) override {
        WorkspaceEditRequest request{detail::WorkspaceEditAccess::entries(edit), {}, metadata};
        for (const auto& document : documents_.textDocuments())
            request.versions.insert(document->uri().toString(QUrl::FullyEncoded), document->version());
        return requests_.applyEdit(request);
    }
    QVector<std::shared_ptr<TextDocument>> textDocuments() const override { return documents_.textDocuments(); }
    using Workspace::openTextDocument;
    QFuture<std::shared_ptr<TextDocument>> openTextDocument(const QUrl& uri,
        const TextDocumentOpenOptions& options = {}) override {
        return opening_.open(uri, options);
    }
    QFuture<std::shared_ptr<TextDocument>> openTextDocument(const UntitledTextDocumentOptions& options = {}) override {
        return opening_.create(options);
    }
    QFuture<std::optional<QUrl>> save(const QUrl& uri) override { return requests_.save(uri, false); }
    QFuture<std::optional<QUrl>> saveAs(const QUrl& uri) override { return requests_.save(uri, true); }
    QFuture<bool> saveAll(bool includeUntitled = false) override { return requests_.saveAll(includeUntitled); }
    Event<std::shared_ptr<TextDocument>> onDidOpenTextDocument() const override { return documents_.onDidOpenTextDocument(); }
    Event<TextDocumentChangeEvent> onDidChangeTextDocument() const override { return documents_.onDidChangeTextDocument(); }
    Event<std::shared_ptr<TextDocument>> onDidCloseTextDocument() const override { return documents_.onDidCloseTextDocument(); }
    Event<std::shared_ptr<TextDocument>> onDidSaveTextDocument() const override { return documents_.onDidSaveTextDocument(); }
    std::shared_ptr<WorkspaceConfiguration> getConfiguration(const QString& section = {},
        const std::optional<ConfigurationScope>& scope = {}) const override {
        return configuration_.getConfiguration(section, scope);
    }
    Event<ConfigurationChangeEvent> onDidChangeConfiguration() const override {
        return configuration_.onDidChangeConfiguration();
    }
  private:
    HostConfiguration& configuration_;
    HostDocuments& documents_;
    HostWorkspaceRequests& requests_;
    HostDocumentOpening opening_;
    HostContentProviders& contentProviders_;
    LocalFileSystem files_;
};
} // namespace litecode::extensions
