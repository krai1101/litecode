#include "ui/GitWorktreeDialog.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include "ui/components/QuickInputDialog.h"
#include "workspace/GitService.h"
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QPointer>

namespace litecode::ui {
namespace {
class WorktreeRequest final : public QObject {
  public:
    WorktreeRequest(QWidget* owner, workspace::GitService* git)
        : QObject(owner), owner_(owner), git_(git), repository_(git->repository()),
          generation_(git->workspaceGeneration()) {
        connect(&reader_, &workspace::GitService::changed, this, [this] {
            if (!started_ && !reader_.repository().isEmpty()) {
                started_ = true;
                reader_.listBranches();
            }
        });
        connect(&reader_, &workspace::GitService::error, this, [this](const QString& message) {
            ConfirmationDialog::showMessage(owner_, tr("Create Worktree"), message);
            deleteLater();
        });
        connect(&reader_, &workspace::GitService::branchesReady, this,
                [this](const QVector<workspace::GitBranch>& branches) {
                    if (!current()) {
                        deleteLater();
                        return;
                    }
                    QVector<QuickPickItem> items{{"new", tr("Create New Branch...")}};
                    for (const auto& branch : branches) {
                        if (!branch.current && !branch.ref.endsWith("/HEAD"))
                            items.append({branch.ref, branch.name, branch.commit.left(7)});
                    }
                    QuickOpenDialog picker(tr("Create Worktree"), tr("Select a branch or tag"),
                                           items, {}, owner_);
                    if (picker.exec() != QDialog::Accepted || !current()) {
                        deleteLater();
                        return;
                    }
                    const QString selection = picker.selectedItemId();
                    QString newBranch;
                    if (selection == "new") {
                        components::QuickInputDialog nameInput(tr("Create Worktree"),
                                                               tr("New branch name"),
                                                               tr("Branch name"), owner_);
                        if (nameInput.exec() != QDialog::Accepted || !current()) {
                            deleteLater();
                            return;
                        }
                        newBranch = nameInput.value();
                    }
                    QString label = newBranch;
                    if (label.isEmpty())
                        for (const auto& branch : branches)
                            if (branch.ref == selection) {
                                label = branch.name;
                                break;
                            }
                    label.replace('/', '-').replace('\\', '-');
                    const QFileInfo repositoryInfo(repository_);
                    const QString defaultPath =
                        repositoryInfo.dir().filePath(repositoryInfo.fileName() + "-" + label);
                    components::QuickInputDialog pathInput(
                        tr("Create Worktree"), tr("Worktree path"), tr("Worktree path"), owner_);
                    pathInput.input()->setText(defaultPath);
                    if (pathInput.exec() == QDialog::Accepted && current())
                        git_->createWorktree(pathInput.value(), selection, newBranch);
                    deleteLater();
                });
        reader_.setWorkspace(repository_);
    }

  private:
    bool current() const {
        return owner_ && git_ && git_->repository() == repository_ &&
               git_->workspaceGeneration() == generation_ && !git_->busy();
    }
    QPointer<QWidget> owner_;
    QPointer<workspace::GitService> git_;
    QString repository_;
    quint64 generation_{};
    bool started_{};
    workspace::GitService reader_;
};
class WorktreeDeleteRequest final : public QObject {
  public:
    WorktreeDeleteRequest(QWidget* owner, workspace::GitService* git)
        : QObject(owner), owner_(owner), git_(git), repository_(git->repository()),
          generation_(git->workspaceGeneration()) {
        connect(
            git, &workspace::GitService::worktreesReady, this, [this](const QStringList& paths) {
                if (!current()) {
                    deleteLater();
                    return;
                }
                QVector<QuickPickItem> items;
                const auto main = QDir(repository_).canonicalPath();
                for (const auto& path : paths) {
                    if (QDir(path).canonicalPath() != main && QFileInfo(path).isDir())
                        items.append({path, QFileInfo(path).fileName(), path});
                }
                if (items.isEmpty()) {
                    ConfirmationDialog::showMessage(owner_, tr("Delete Worktree"),
                                                    tr("This repository has no linked worktrees."));
                    deleteLater();
                    return;
                }
                QuickOpenDialog picker(tr("Delete Worktree"), tr("Select a worktree to delete"),
                                       items, {}, owner_);
                if (picker.exec() != QDialog::Accepted || !current()) {
                    deleteLater();
                    return;
                }
                const QString target = picker.selectedItemId();
                if (ConfirmationDialog::ask(owner_, tr("Delete Worktree"),
                                            tr("Remove this worktree folder? Git will refuse if it "
                                               "has uncommitted changes.\n%1")
                                                .arg(target),
                                            tr("Delete Worktree"), {},
                                            tr("Cancel")) == ConfirmationChoice::Primary &&
                    current())
                    git_->deleteWorktree(target);
                deleteLater();
            });
        git_->listWorktrees();
    }

  private:
    bool current() const {
        return owner_ && git_ && git_->repository() == repository_ &&
               git_->workspaceGeneration() == generation_ && !git_->busy();
    }
    QPointer<QWidget> owner_;
    QPointer<workspace::GitService> git_;
    QString repository_;
    quint64 generation_{};
};
} // namespace

void requestGitWorktree(QWidget* owner, workspace::GitService* git) {
    if (!owner || !git || git->busy() || git->repository().isEmpty())
        return;
    new WorktreeRequest(owner, git);
}
void requestGitWorktreeDeletion(QWidget* owner, workspace::GitService* git) {
    if (!owner || !git || git->busy() || git->repository().isEmpty())
        return;
    new WorktreeDeleteRequest(owner, git);
}
} // namespace litecode::ui
