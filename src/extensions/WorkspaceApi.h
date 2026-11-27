#pragma once
#include "extensions/ConfigurationApi.h"
#include "extensions/ContentProviderApi.h"
#include "extensions/DocumentApi.h"
#include "extensions/DocumentOpenApi.h"
#include "extensions/FileSystemApi.h"
#include "extensions/WorkspaceEditApi.h"

namespace litecode::extensions {
// Native surface of vscode.workspace. Selected groups are still experimental;
// declaration presence alone is not a claim of full workspace parity.
class Workspace {
  public:
    virtual ~Workspace() = default;
    virtual QFuture<bool> applyEdit(const WorkspaceEdit& edit,
                                    const std::optional<WorkspaceEditMetadata>& metadata = {}) = 0;
    [[nodiscard]] virtual FileSystem& fs() = 0;
    virtual Disposable
    registerTextDocumentContentProvider(const QString& scheme,
                                        std::shared_ptr<TextDocumentContentProvider> provider) = 0;
    [[nodiscard]] virtual QVector<std::shared_ptr<TextDocument>> textDocuments() const = 0;
    virtual QFuture<std::shared_ptr<TextDocument>>
    openTextDocument(const QUrl& uri, const TextDocumentOpenOptions& options = {}) = 0;
    QFuture<std::shared_ptr<TextDocument>>
    openTextDocument(const QString& path, const TextDocumentOpenOptions& options = {}) {
        return openTextDocument(QUrl::fromLocalFile(path), options);
    }
    virtual QFuture<std::shared_ptr<TextDocument>>
    openTextDocument(const UntitledTextDocumentOptions& options = {}) = 0;
    virtual QFuture<std::optional<QUrl>> save(const QUrl& uri) = 0;
    virtual QFuture<std::optional<QUrl>> saveAs(const QUrl& uri) = 0;
    virtual QFuture<bool> saveAll(bool includeUntitled = false) = 0;
    [[nodiscard]] virtual Event<std::shared_ptr<TextDocument>> onDidOpenTextDocument() const = 0;
    [[nodiscard]] virtual Event<TextDocumentChangeEvent> onDidChangeTextDocument() const = 0;
    [[nodiscard]] virtual Event<std::shared_ptr<TextDocument>> onDidCloseTextDocument() const = 0;
    [[nodiscard]] virtual Event<std::shared_ptr<TextDocument>> onDidSaveTextDocument() const = 0;
    [[nodiscard]] virtual std::shared_ptr<WorkspaceConfiguration>
    getConfiguration(const QString& section = {},
                     const std::optional<ConfigurationScope>& scope = {}) const = 0;
    [[nodiscard]] std::shared_ptr<WorkspaceConfiguration> getConfiguration(const QString& section,
                                                                           const QUrl& uri) const {
        return getConfiguration(section, ConfigurationScope{uri, {}});
    }
    [[nodiscard]] virtual Event<ConfigurationChangeEvent> onDidChangeConfiguration() const = 0;
};
} // namespace litecode::extensions
