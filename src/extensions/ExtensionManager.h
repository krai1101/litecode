#pragma once
#include "extensions/ChatHistory.h"
#include "extensions/ChatStreamLedger.h"
#include "extensions/ConfigurationRegistry.h"
#include "extensions/EditorStateWire.h"
#include "extensions/ExtensionApi.h"
#include "extensions/TextEditorEditData.h"
#include "extensions/WindowStateWire.h"
#include "extensions/WorkbenchClipboard.h"
#include "extensions/WorkbenchContentProviders.h"
#include "extensions/WorkbenchDebugConfigurations.h"
#include "extensions/WorkbenchDebugSessions.h"
#include "extensions/WorkbenchDebugStarts.h"
#include "extensions/WorkbenchDebugTrackers.h"
#include "extensions/WorkbenchDialogs.h"
#include "extensions/WorkbenchDocuments.h"
#include "extensions/WorkbenchEnvironment.h"
#include "extensions/WorkbenchExternalUris.h"
#include "extensions/WorkbenchInputBox.h"
#include "extensions/WorkbenchMessages.h"
#include "extensions/WorkbenchNativeViews.h"
#include "extensions/WorkbenchOutput.h"
#include "extensions/WorkbenchProgress.h"
#include "extensions/WorkbenchQuickInputCoordinator.h"
#include "extensions/WorkbenchQuickPick.h"
#include "extensions/WorkbenchStatusBar.h"
#include "extensions/WorkbenchUriHandlers.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <functional>
#include <memory>
#include <vector>

class QPluginLoader;
namespace litecode::extensions {
class ConfigurationService;
struct ExtensionInfo {
    QString id, name, version, path, error, iconPath, description;
    bool enabled{}, active{};
    bool uninstalling{};
};
struct ExtensionCommand {
    QString id, title, category, extensionId;
};
struct DebuggerContribution {
    QString type, extensionId;
    QJsonObject configurationAttributes;
    QString label;
    QJsonArray initialConfigurations;
};
struct FormatterContribution {
    QString languageId, extensionId, extensionName;
};
struct ChatParticipantContribution {
    QString id, name, fullName, description, extensionId;
    bool isSticky{};
    QJsonArray commands;
    QJsonArray disambiguation;
    QJsonValue iconPath;
    bool hasFollowups{};
};
class ExtensionManager final : public QObject {
    Q_OBJECT
  public:
    explicit ExtensionManager(const QString& directory, QObject* parent = nullptr);
    ~ExtensionManager() override;
    void setHostExecutablePath(const QString& path);
    void setDefaultShell(const QString& path);
    // Private workbench state source, not an extension-facing setter.
    void setWindowState(WindowState state);
    [[nodiscard]] WindowState windowState() const { return windowState_; }
    void
    setEditorStateSource(std::function<WindowEditorState()> source,
                         std::function<bool(const QString&, const TextEditorEditData&)> applyEdit);
    void setEditorState(const WindowEditorState& state);
    void setWorkbenchCommandSource(std::function<QStringList()> ids,
                                   std::function<bool(const QString&)> invoke);
    void discover();
    QVector<ExtensionInfo> extensions() const;
    bool setEnabled(const QString& id, bool enabled);
    bool uninstall(const QString& id);
    void setWorkspace(const QString& path);
    void activateForLanguage(const QString& languageId);
    // Activates the owner of a contributed participant when that participant
    // is selected. This only starts the host; request handling is not public yet.
    bool activateForChatParticipant(const QString& participantId);
    bool executeCommand(const QString& id);
    // Internal asynchronous workbench/host operation; not part of the plugin SDK.
    quint64 requestCommand(const QString& id, const QJsonArray& arguments = {},
                           const QString& owner = {});
    void setDiffCommandHandler(
        std::function<QFuture<QJsonValue>(const QString&, const QJsonArray&)> handler) {
        diffCommand_ = std::move(handler);
    }
    QStringList commands() const;
    QVector<ExtensionCommand> commandContributions() const;
    QVector<DebuggerContribution> debuggerContributions() const;
    QSet<QString> breakpointLanguages() const;
    QVector<FormatterContribution> formatterContributions(const QString& languageId) const;
    QVector<ChatParticipantContribution> chatParticipantContributions() const;
    // Private workbench transport; this is not the public extension chat API.
    quint64 requestChat(const QString& sessionId, const QString& participantId,
                        const QJsonObject& request);
    bool cancelChat(quint64 requestId);
    bool sendChatFeedback(quint64 requestId, ChatResultFeedbackKind kind);
    void closeChatSession(const QString& sessionId);
    [[nodiscard]] QJsonArray chatHistory(const QString& sessionId,
                                         const QString& participantId) const;
    quint64 requestDocumentFormat(const QString& languageId, const QString& text,
                                  const QString& extensionId = {});
    quint64 requestDebugAdapter(const QString& type, const QJsonObject& configuration,
                                const QString& sessionId = {},
                                const QJsonValue& folderOverride = QJsonValue::Undefined);
    // Releases a pending descriptor or a host-owned inline instance. Private IPC.
    bool releaseDebugAdapter(quint64 requestId);
    quint64 requestDebugConfiguration(const QString& type, const QString& method,
                                      const QJsonValue& configuration,
                                      DebugConfigurationProviderTriggerKind trigger =
                                          DebugConfigurationProviderTriggerKind::Initial,
                                      const QJsonValue& folderOverride = QJsonValue::Undefined);
    bool cancelDebugConfiguration(quint64 requestId);
    void setDebugSessionHandler(WorkbenchDebugSessions::Handler handler);
    void setDebugStartHandler(WorkbenchDebugStarts::Handler handler);
    void publishDebugSessionEvent(const QJsonObject& event);
    void publishDebugTrackerEvent(const QString& sessionId, const QJsonObject& event);
    static QString validateManifest(const QJsonObject& manifest);
    // Workbench configuration service access; this is not a plugin SDK API.
    [[nodiscard]] const ConfigurationRegistry& configurationRegistry() const {
        return configurationRegistry_;
    }
    [[nodiscard]] ConfigurationService& configuration() { return *configurationService_; }
    [[nodiscard]] WorkbenchDocuments& documents() { return *documents_; }
    [[nodiscard]] WorkbenchContentProviders& contentProviders() { return *contentProviders_; }
    [[nodiscard]] WorkbenchOutput& outputs() { return *outputs_; }
    [[nodiscard]] WorkbenchMessages& messages() { return *messages_; }
    [[nodiscard]] WorkbenchDialogs& dialogs() { return *dialogs_; }
    [[nodiscard]] WorkbenchInputBox& inputBox() { return *inputBox_; }
    [[nodiscard]] WorkbenchQuickPick& quickPick() { return *quickPick_; }
    [[nodiscard]] WorkbenchStatusBar& statusBar() { return *statusBar_; }
    [[nodiscard]] WorkbenchExternalUris& externalUris() { return *externalUris_; }
    // Private workbench boundary: caller has obtained user permission.
    bool dispatchUri(const QUrl& uri);
    std::optional<ExtensionInfo> uriTarget(const QUrl& uri) const;
    [[nodiscard]] WorkbenchProgress& progress() { return *progress_; }
    [[nodiscard]] WorkbenchClipboard& clipboard() { return clipboard_; }
    WorkbenchNativeViews& nativeViews() { return nativeViews_; }
    QVector<NativeViewContribution> nativeViewContributions() const;
  signals:
    void uninstallFinished(const QString& id, bool removed, const QString& error);
    void changed();
    void configurationContributionsChanged();
    void openFileRequested(const QString& path);
    void informationMessage(const QString& message);
    void commandResult(quint64 requestId, const QJsonValue& result, const QString& error);
    void uriError(const QString& owner, const QString& error);
    void uriDispatched(const QString& owner);
    void documentFormatResult(quint64 requestId, const QString& text, const QString& error);
    void debugAdapterResult(quint64 requestId, const QJsonObject& descriptor, const QString& error);
    void debugConfigurationResult(quint64 requestId, const QJsonValue& result,
                                  const QString& error);
    void debugSessionOwnerUnavailable(const QString& sessionId);
    void chatResponsePart(quint64 requestId, quint64 sequence, const QJsonObject& part);
    void chatRequestFinished(quint64 requestId, const QJsonObject& result, const QString& error);
    void chatFollowupsReady(quint64 requestId, const QJsonArray& followups, const QString& error);

  private:
    struct Entry;
    struct CompletedChat {
        Entry* owner{};
        QString sessionId;
        QString participantId;
        QJsonObject result;
        bool followupsDelivered{};
    };
    bool activate(Entry& entry);
    void deactivate(Entry& entry);
    void handleHostMessage(Entry& entry, const QJsonObject& message);
    void sendHostMessage(Entry& entry, const QJsonObject& message);
    void failDebugRequests(Entry& entry, const QString& error);
    void sendDebugAdapterRequest(Entry& entry, quint64 id, const QString& type,
                                 const QJsonObject& session);
    void failCommandRequests(Entry& entry, const QString& error);
    void failChatRequests(Entry& entry, const QString& error);
    void dispatchQueuedChatRequests(Entry& entry);
    void discardForwardedCommandRequests(Entry& source);
    void rebuildConfigurationContributions();
    struct ForwardedCommand {
        Entry* source{};
        quint64 sourceRequestId{};
    };
    QString directory_, workspace_, hostExecutablePath_;
    WorkbenchNativeViews nativeViews_;
    std::function<QStringList()> workbenchCommandIds_;
    std::function<bool(const QString&)> invokeWorkbenchCommand_;
    std::function<QFuture<QJsonValue>(const QString&, const QJsonArray&)> diffCommand_;
    quint64 nextFormatRequestId_{};
    quint64 nextCommandRequestId_{};
    quint64 nextDebugAdapterRequestId_{};
    quint64 nextChatRequestId_{};
    ChatStreamLedger chatStreams_;
    ConfigurationRegistry configurationRegistry_;
    std::unique_ptr<ConfigurationService> configurationService_;
    std::unique_ptr<WorkbenchDocuments> documents_;
    std::unique_ptr<WorkbenchContentProviders> contentProviders_;
    std::unique_ptr<WorkbenchOutput> outputs_;
    std::unique_ptr<WorkbenchMessages> messages_;
    std::unique_ptr<WorkbenchDialogs> dialogs_;
    std::unique_ptr<WorkbenchInputBox> inputBox_;
    std::unique_ptr<WorkbenchQuickPick> quickPick_;
    std::unique_ptr<WorkbenchQuickInputCoordinator> quickInputCoordinator_;
    std::unique_ptr<WorkbenchStatusBar> statusBar_;
    std::unique_ptr<WorkbenchExternalUris> externalUris_;
    std::unique_ptr<WorkbenchUriHandlers> uriHandlers_;
    QSet<quint64> statusBarCommandRequests_;
    std::unique_ptr<WorkbenchProgress> progress_;
    WorkbenchClipboard clipboard_;
    std::unique_ptr<WorkbenchEnvironment> environment_;
    std::unique_ptr<WorkbenchDebugConfigurations> debugConfigurations_;
    std::unique_ptr<WorkbenchDebugSessions> debugSessions_;
    std::unique_ptr<WorkbenchDebugStarts> debugStarts_;
    std::unique_ptr<WorkbenchDebugTrackers> debugTrackers_;
    QString defaultShell_;
    WindowState windowState_{true, true};
    WindowEditorState editorState_;
    std::function<WindowEditorState()> editorStateSource_;
    std::function<bool(const QString&, const TextEditorEditData&)> editorEdit_;
    QHash<QString, ChatHistory> chatHistories_;
    QHash<quint64, CompletedChat> completedChats_;
    QHash<quint64, ForwardedCommand> forwardedCommandRequests_;
    std::vector<std::unique_ptr<Entry>> entries_;
};
} // namespace litecode::extensions
