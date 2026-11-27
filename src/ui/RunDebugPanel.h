#pragma once
#include "debug/DebugConfiguration.h"
#include "debug/DebugSession.h"
#include "debug/TaskRunner.h"
#include "ui/components/SidebarView.h"
#include "extensions/CancellationApi.h"
#include <QPromise>
#include <QPointer>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QWidget>

class QLabel;
class QProcess;
class QPushButton;
class QToolButton;

namespace litecode::core { class SettingsService; }

namespace litecode::ui {
namespace components { class ComboBox; }
class DebugInspectorWidget;
class RunDebugPanel final : public components::SidebarView {
    Q_OBJECT
  public:
    explicit RunDebugPanel(QWidget* parent = nullptr,
                           core::SettingsService* settings = nullptr);
    ~RunDebugPanel() override;
    void setWorkspace(const QString& path);
    void setDebuggerSchemas(const QHash<QString, QJsonObject>& schemas);
    void setDebuggerChoices(const QList<QPair<QString, QString>>& choices);
    void selectDynamicConfiguration(const QJsonObject& configuration);
    void startDebugging();
    void startSelectedConfiguration(const QJsonObject& configuration);
    QFuture<bool> startFromExtension(const QJsonValue& folder, const QJsonValue& configuration,
        const QJsonObject& options, extensions::CancellationToken cancellation);
    QJsonValue launchFolder() const { return launchFolder_; }
    void requestConfiguration();
    void runWithoutDebugging();
    void completeDebugAdapterRequest(quint64 requestId, const QJsonObject& descriptor, const QString& error);
    void completeDebugConfigurationRequest(quint64 requestId, const QJsonValue& configuration, const QString& error);
    void setVariableContextSource(std::function<debug::VariableContext()> source) { variableContextSource_ = std::move(source); }
    void stop();
    void cancelOwnedDebugSession(const QString& sessionId);
    void continueDebugging();
    void pauseDebugging();
    void stepOver();
    void stepInto();
    void stepOut();
    void evaluateConsole(const QString& expression);
    void toggleBreakpoint(const QString& path, int line);
    void setBreakpointLanguages(const QSet<QString>& languages);
    bool hasBreakpoint(const QString& path, int line) const;
    QString outputText() const;
    QWidget* takeTitleActions();
    bool hasConfigurations() const;
    void setActiveFile(const QString& path);
    bool isPaused() const;
    void setIntegratedTerminalAvailable(bool available);
    bool usesIntegratedTerminal() const { return integratedTerminalActive_; }
    bool respondToRunInTerminal(const QJsonObject& request, bool success,
                                const QString& error = {}, const QJsonObject& body = {});
    QFuture<QJsonValue> handleDebugSessionRequest(const QString& id, const QString& operation,
        const QString& command, const QJsonValue& arguments);
  signals:
    void outputReceived(const QString& text);
    void outputReset();
    void consoleAvailabilityChanged(bool available);
    void titleActionsAvailabilityChanged(bool available);
    void configurationRequested();
    void openFileRequested();
    void openLocationRequested(const QString& path, int line);
    void pausedLocationChanged(const QString& path, int line);
    void breakpointsChanged(const QMap<QString, QSet<int>>& breakpoints);
    void runInTerminalRequested(const QJsonObject& request);
    void debugAdapterRequested(quint64 requestId, const QString& type,
                               const QJsonObject& configuration, const QString& sessionId);
    void debugAdapterReleased(quint64 requestId);
    void debugConfigurationRequested(quint64 requestId, const QString& type, const QString& method, const QJsonObject& configuration);
    void debugConfigurationCanceled(quint64 requestId);
    void selectDebugConfigurationRequested();
    void selectDynamicConfigurationRequested(const QString& type);
    void debugSessionEvent(const QJsonObject& event);
    void debugTrackerEvent(const QString& sessionId, const QJsonObject& event);
  private:
    bool prepare(QJsonObject* configuration, bool resolve = true);
    void requestConfigurationStep(const QString& method, const QJsonObject& configuration);
    void requestAdapter(const QJsonObject& configuration);
    void beginStart(bool debug);
    bool canStart() const;
    void continuePreparedStart(const QJsonObject& configuration, bool debug);
    void startPrepared(const QJsonObject& configuration, bool debug);
    void runPostTask();
    void updateControls();
    void releaseAdapter();
    QString workspace_;
    QString activeFile_;
    core::SettingsService* settings_{};
    QVector<debug::DebugConfiguration> configurations_;
    std::optional<debug::DebugConfiguration> dynamicConfiguration_;
    QHash<QString, QJsonObject> debuggerSchemas_;
    debug::DebugSession session_;
    debug::TaskRunner taskRunner_;
    QJsonObject pendingConfiguration_;
    QJsonObject adapterDescriptor_;
    QString debugSessionId_;
    QJsonObject debugSessionContext_;
    bool debugSessionPublished_{};
    bool pendingStart_{};
    bool pendingDebug_{};
    quint64 nextAdapterRequestId_{};
    quint64 pendingAdapterRequestId_{};
    quint64 ownedAdapterRequestId_{};
    quint64 nextConfigurationRequestId_{}, pendingConfigurationRequestId_{};
    QString configurationPhase_;
    debug::VariableContext launchVariables_;
    std::function<debug::VariableContext()> variableContextSource_;
    bool integratedTerminalActive_{};
    QString postDebugTask_;
    QString launchWorkspace_;
    QProcess* runProcess_{};
    components::ComboBox* selector_{};
    QPointer<QWidget> toolbar_;
    QWidget* launchGroup_{};
    QWidget* sessionControls_{};
    QLabel* notice_{};
    QString output_;
    QToolButton* debugButton_{};
    QToolButton* configureButton_{};
    QToolButton* pauseContinueButton_{};
    QToolButton* stepOverButton_{};
    QToolButton* stepIntoButton_{};
    QToolButton* stepOutButton_{};
    QToolButton* stopButton_{};
    QWidget* welcome_{};
    QWidget* welcomeDivider_{};
    QLabel* welcomePrompt_{};
    DebugInspectorWidget* inspector_{};
    QMap<QString, QSet<int>> breakpoints_;
    std::optional<QSet<QString>> breakpointLanguages_;
    bool supportsBreakpoints(const QString& path) const;
    QSet<int> consoleRequests_;
    void refreshBreakpoints();
    void finishDebugStart(bool success, const QString& error = {});
    std::shared_ptr<QPromise<bool>> debugStartResult_;
    extensions::Disposable debugStartCancellation_;
    QJsonValue launchFolder_{QJsonValue::Undefined};
    QJsonObject debugStartOptions_;
    void appendOutput(const QString& text);
    void showNotice(const QString& text);
};
} // namespace litecode::ui
