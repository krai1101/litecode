#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/ChatApi.h"
#include "extensions/CommandApi.h"
#include "extensions/ConfigurationApi.h"
#include "extensions/WorkspaceApi.h"
#include "extensions/MarkdownApi.h"
#include "extensions/ThemeApi.h"
#include "extensions/WindowApi.h"
#include "extensions/TextApi.h"
#include "extensions/SnippetApi.h"
#include "extensions/EnvironmentApi.h"
#include "extensions/DocumentApi.h"
#include "extensions/EditorEditApi.h"
#include "extensions/DebugApi.h"
// Experimental API 20. This is not yet the complete VS Code extension API. Do not extend
// these legacy context methods for new public capabilities; the replacement
// contract is tracked in docs/extension-api-contract.md.
#include <QObject>
#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QtPlugin>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace litecode::extensions {
inline constexpr int ApiVersion = 25;

// Native equivalent of a VS Code command handler's immediate value or Promise.
using CommandResult = std::variant<QJsonValue, QFuture<QJsonValue>>;
using CommandHandler = std::function<CommandResult(const QJsonArray&)>;

class Disposable;
// C++ service equivalent of vscode.commands. All calls are non-blocking across
// the separate native extension host. JSON values are the current IPC value
// domain; editor-specific VS Code types are not yet represented by this SDK.
class Commands {
  public:
    virtual ~Commands() = default;
    [[nodiscard]] virtual Disposable registerCommand(const QString& id,
                                                     CommandHandler callback) = 0;
    virtual QFuture<QJsonValue> executeCommand(const QString& id,
                                               const QJsonArray& arguments = {}) = 0;
    virtual QFuture<QStringList> getCommands(bool filterInternal = false) = 0;
};

// Registrations belong to this context and are removed when the extension is disabled.
class Memento {
  public:
    virtual ~Memento() = default;
    [[nodiscard]] virtual QStringList keys() const = 0;
    [[nodiscard]] virtual QJsonValue get(const QString& key) const = 0;
    [[nodiscard]] QJsonValue get(const QString& key, const QJsonValue& defaultValue) const {
        const auto value = get(key);
        return value.isUndefined() ? defaultValue : value;
    }
    virtual QFuture<void> update(const QString& key, const QJsonValue& value) = 0;
};
class GlobalMemento : public Memento {
  public:
    virtual void setKeysForSync(const QStringList& keys) = 0;
};
struct SecretStorageChangeEvent final { QString key; };
class SecretStorage {
  public:
    virtual ~SecretStorage() = default;
    virtual QFuture<QStringList> keys() = 0;
    virtual QFuture<std::optional<QString>> get(const QString& key) = 0;
    virtual QFuture<void> store(const QString& key, const QString& value) = 0;
    // "delete" is a C++ keyword; delete_ is the native spelling of that member.
    virtual QFuture<void> delete_(const QString& key) = 0;
    [[nodiscard]] virtual Event<SecretStorageChangeEvent> onDidChange() const = 0;
};
class ExtensionContext : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual QString extensionId() const = 0;
    virtual Memento& workspaceState() = 0;
    virtual GlobalMemento& globalState() = 0;
    virtual SecretStorage& secrets() = 0;
    virtual QUrl extensionUri() const = 0;
    virtual QString extensionPath() const = 0;
    virtual QString asAbsolutePath(const QString& relativePath) const = 0;
    [[nodiscard]] virtual std::optional<QUrl> storageUri() const = 0;
    [[nodiscard]] virtual QUrl globalStorageUri() const = 0;
    [[nodiscard]] virtual QUrl logUri() const = 0;
    [[nodiscard]] std::optional<QString> storagePath() const {
        const auto uri = storageUri();
        return uri ? std::optional<QString>(uri->toLocalFile()) : std::nullopt;
    }
    [[nodiscard]] QString globalStoragePath() const { return globalStorageUri().toLocalFile(); }
    [[nodiscard]] QString logPath() const { return logUri().toLocalFile(); }
    virtual QString workspacePath() const = 0;
    virtual Commands& commands() = 0;
    virtual Workspace& workspace() = 0;
    virtual Window& window() = 0;
    virtual Environment& env() = 0;
    virtual Chat& chat() = 0;
    virtual Debug& debug() = 0;
    [[nodiscard]] virtual Disposable registerCommand(const QString& id,
                                                     CommandHandler callback) = 0;
    // Called in the extension host. The workbench sends an immutable UTF-8 text
    // snapshot and applies the result only if the document has not changed.
    [[nodiscard]] virtual Disposable registerDocumentFormatter(
        const QString& languageId, std::function<QString(const QString&)> formatter) = 0;
    virtual void openFile(const QString& path) = 0;
    virtual void showInformationMessage(const QString& message) = 0;
    std::vector<Disposable> subscriptions;
    void disposeSubscriptions() noexcept {
        // Detach before entering extension code: a cleanup callback may clear,
        // append to, or recursively dispose the public subscription vector.
        // Dispose in registration order, as upstream's dispose(array) does.
        detail::disposeSubscriptions(subscriptions);
    }
  signals:
    void workspaceChanged(const QString& path);
};
class Extension {
  public:
    virtual ~Extension() = default;
    virtual void activate(ExtensionContext& context) = 0;
    virtual void deactivate() = 0;
};
} // namespace litecode::extensions
#define LITECODE_EXTENSION_IID "org.litecode.Extension/16.0"
Q_DECLARE_INTERFACE(litecode::extensions::Extension, LITECODE_EXTENSION_IID)
