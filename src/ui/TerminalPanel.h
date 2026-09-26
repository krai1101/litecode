#pragma once

#include "core/OperationResult.h"

#include <QFont>
#include <QWidget>

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

class TerminalPanel final : public QWidget {
    Q_OBJECT
  public:
    using BackendFactory = std::function<std::unique_ptr<terminal::ITerminalBackend>()>;

    explicit TerminalPanel(QWidget* parent = nullptr, BackendFactory backendFactory = {});
    ~TerminalPanel() override;
    void setWorkingDirectory(const QString& path);
    void openInDirectory(const QString& path);
    [[nodiscard]] QWidget* controlsWidget() const;
    void showFind();
    void newTerminal();
    [[nodiscard]] bool hasTerminals() const;
    void setDefaultProfile(const QString& profileId);
    void setTerminalFont(const QFont& font);

  signals:
    void closePanelRequested();
    void toggleMaximizeRequested();
    void fileLinkActivated(const QString& filePath, int line, int column);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    struct Session;

    void start();
    void startWithProfile(const QString& profileId, const QString& workingDirectory = {});
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

    QString workingDirectory_;
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
};

} // namespace litecode::ui
