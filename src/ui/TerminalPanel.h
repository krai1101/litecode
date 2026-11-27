#pragma once

#include "core/OperationResult.h"
#include "terminal/TerminalTypes.h"

#include <QFont>
#include <QWidget>
#include <QHash>
#include <QPointer>

#include <functional>
#include <memory>
#include <vector>

class QListWidget;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace litecode::terminal {
class ITerminalBackend;
class TerminalSessionService;
} // namespace litecode::terminal

namespace litecode::ui {
class DebugTerminalLaunch;
class TerminalView;

class TerminalPanel final : public QWidget {
    Q_OBJECT
  public:
    using BackendFactory = std::function<std::unique_ptr<terminal::ITerminalBackend>()>;

    explicit TerminalPanel(QWidget* parent = nullptr, BackendFactory backendFactory = {});
    ~TerminalPanel() override;
    void setWorkingDirectory(const QString& path);
    void openInDirectory(const QString& path);
    [[nodiscard]] QWidget* controlsWidget() const;
    void setEmbeddedPanel(bool embedded);
    void showFind();
    void newTerminal();
    [[nodiscard]] bool hasTerminals() const;
    void setDefaultProfile(const QString& profileId);
    [[nodiscard]] QString defaultShell() const;
    void setTerminalFont(const QFont& font);
    [[nodiscard]] core::OperationId runProcess(const terminal::TerminalProfile& process,
                                               const QString& workingDirectory);
    [[nodiscard]] core::OperationId runDebugCommand(const QString& title,
                                                    const QStringList& command,
                                                    const QProcessEnvironment& environment,
                                                    const QString& workingDirectory,
                                                    bool argsCanBeInterpretedByShell = false);
    void cancelDebugCommand(core::OperationId id);
    void releaseDebugTerminal(core::OperationId id);
    void stopProcess(core::OperationId id);

  signals:
    void closePanelRequested();
    void toggleMaximizeRequested();
    void fileLinkActivated(const QString& filePath, int line, int column);
    void processCompleted(const litecode::terminal::TerminalExit& result);
    void defaultShellChanged(const QString& path);
    void debugCommandReady(litecode::core::OperationId id, qint64 shellProcessId, const QString& error);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    struct Session;

    void start();
    void startWithProfile(const QString& profileId, const QString& workingDirectory = {});
    void createSessionView(core::OperationId id, const terminal::TerminalProfile& profile,
                           const QString& workingDirectory);
    void removeSession(int index);
    void removeSessionView(core::OperationId id);
    void activateSession(int index);
    void rebuildSessionList();
    void scheduleTerminalResize();
    void resizeBackend(Session& session);
    void updateFind();
    void updateFindResult(int current, int total);
    void positionFindBar();
    [[nodiscard]] Session* activeSession();
    [[nodiscard]] Session* findSession(core::OperationId id);
    [[nodiscard]] Session* findSession(const TerminalView* view);

    QString workingDirectory_;
    QHash<quint64, QPointer<DebugTerminalLaunch>> debugLaunches_;
    QString defaultProfileId_;
    QFont terminalFont_{QStringLiteral("Consolas"), 11};
    std::unique_ptr<terminal::TerminalSessionService> sessionService_;
    std::vector<std::unique_ptr<Session>> sessions_;
    int activeSessionIndex_{-1};
    QWidget* controls_{};
    QStackedWidget* outputs_{};
    QListWidget* sessionList_{};
    QTimer* resizeTimer_{};
    QFrame* findBar_{};
    QLineEdit* findInput_{};
    QLabel* findCount_{};
    QToolButton* matchCase_{};
    QToolButton* wholeWord_{};
    QToolButton* regularExpression_{};
    QToolButton* deleteSession_{};
    QToolButton* maximizePanel_{};
    QToolButton* closePanel_{};
};

} // namespace litecode::ui
