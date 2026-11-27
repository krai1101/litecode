#pragma once
#include "core/GitPreferences.h"
#include "workspace/GitService.h"
#include <QHash>
#include <QWidget>
#include <functional>
#include <utility>
class QLabel;
class QTreeWidget;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
namespace litecode::workspace {
class GitService;
}
namespace litecode::ui {
class GitHistoryWidget;
class SourceControlPanel final : public QWidget {
    Q_OBJECT
  public:
    explicit SourceControlPanel(QWidget* parent = nullptr);
    void setWorkspace(const QString& path);
    void selectRepository(const QString& path);
    void deleteWorktree();
    void setGitPreferences(const core::GitPreferences& preferences) {
        preferences_ = preferences;
        git_->setPreferences(preferences);
    }
    const core::GitPreferences& gitPreferences() const { return preferences_; }
    void setWorktreeChangeGuard(std::function<bool()> guard) {
        worktreeChangeGuard_ = std::move(guard);
    }
  signals:
    void graphViewChanged(bool graph);
    void gitPreferencesChanged();
    void openFileRequested(const QString& path);
    void comparisonRequested(const litecode::workspace::GitComparison& comparison);
    void gitStatusChanged(const QString& repository, const litecode::workspace::GitStatus& status);
    void repositoryCloned(const QString& path);
    void worktreeCreated(const QString& path);
    void openWorktreeRequested(const QString& path, bool newWindow);
    void linkedWorktreeDeletionRequested(const QString& mainRepository,
                                         const QString& linkedWorktree);
    void gitOutputRequested();
    void gitOutputLine(const QString& line);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

  private:
    void render();
    void transfer(bool stage);
    workspace::GitService* git_{};
    GitHistoryWidget* history_{};
    QLabel* summary_{};
    QLabel* notice_{};
    QLabel* empty_{};
    QLabel* emptyDocs_{};
    QWidget* emptyWelcome_{};
    QTreeWidget* changes_{};
    QPlainTextEdit* message_{};
    QPushButton* commit_{};
    QPushButton* initialize_{};
    QToolButton* refresh_{};
    void showDiff();
    void updateActions();
    void chooseBranch();
    void createBranch(bool from = false);
    void recoverCheckout(const QString& ref, bool detached);
    void chooseRemote(bool publish);
    bool choosingBranch_{}, choosingRemote_{}, publishing_{};
    core::GitPreferences preferences_;
    bool creatingFrom_{};
    bool restoringRecovery_{};
    quint64 workspaceGeneration_{};
    QString workspaceRoot_;
    QHash<QString, QString> commitMessages_;
    std::function<bool()> worktreeChangeGuard_;
    bool allowWorktreeChange() const { return !worktreeChangeGuard_ || worktreeChangeGuard_(); }
};
} // namespace litecode::ui
