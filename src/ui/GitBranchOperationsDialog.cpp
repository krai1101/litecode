#include "ui/GitBranchOperationsDialog.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include "ui/components/QuickInputDialog.h"
#include "workspace/GitService.h"
#include <QPointer>

namespace litecode::ui {
namespace {
class BranchRequest final : public QObject {
  public:
    BranchRequest(QWidget* owner, workspace::GitService* git, GitBranchAction action)
        : QObject(owner), owner_(owner), git_(git), repository_(git->repository()),
          generation_(git->workspaceGeneration()), action_(action) {
        connect(&reader_, &workspace::GitService::changed, this, [this] {
            if (!started_ && !reader_.repository().isEmpty()) {
                started_ = true;
                reader_.listBranches();
            }
        });
        connect(&reader_, &workspace::GitService::error, this, [this](const QString& error) {
            ConfirmationDialog::showMessage(owner_, tr("Branch"), error);
            deleteLater();
        });
        connect(&reader_, &workspace::GitService::branchesReady, this,
                [this](const QVector<workspace::GitBranch>& branches) {
                    if (!current()) {
                        deleteLater();
                        return;
                    }
                    QVector<QuickPickItem> items;
                    for (const auto& branch : branches) {
                        if (branch.current || branch.ref.endsWith("/HEAD"))
                            continue;
                        const bool include = action_ == GitBranchAction::Merge ? true
                                             : action_ == GitBranchAction::DeleteRemote
                                                 ? branch.remote
                                             : action_ == GitBranchAction::DeleteLocal
                                                 ? !branch.remote && !branch.tag
                                                 : !branch.tag;
                        if (include)
                            items.append({branch.ref, branch.name, branch.commit.left(7)});
                    }
                    if (items.isEmpty()) {
                        ConfirmationDialog::showMessage(owner_, tr("Branch"),
                                                        tr("No matching branches."));
                        deleteLater();
                        return;
                    }
                    const auto title = action_ == GitBranchAction::Merge    ? tr("Merge")
                                       : action_ == GitBranchAction::Rebase ? tr("Rebase Branch")
                                       : action_ == GitBranchAction::DeleteRemote
                                           ? tr("Delete Remote Branch")
                                           : tr("Delete Branch");
                    QuickOpenDialog picker(title, tr("Select a branch"), items, {}, owner_);
                    if (picker.exec() != QDialog::Accepted || !current()) {
                        deleteLater();
                        return;
                    }
                    const auto ref = picker.selectedItemId();
                    if (action_ == GitBranchAction::Merge)
                        git_->mergeBranch(ref);
                    else if (action_ == GitBranchAction::Rebase)
                        git_->rebaseBranch(ref);
                    else if (action_ == GitBranchAction::DeleteLocal) {
                        const auto name = ref.mid(11);
                        if (confirm(tr("Delete local branch '%1'? Git will refuse if it is not "
                                       "fully merged.")
                                        .arg(name)))
                            git_->deleteBranch(name);
                    } else {
                        const auto fullName = ref.mid(13);
                        const auto remote = fullName.section('/', 0, 0);
                        const auto name = fullName.mid(remote.size() + 1);
                        if (confirm(tr("Delete branch '%1' from remote '%2'? This changes the "
                                       "remote repository.")
                                        .arg(name, remote)))
                            git_->deleteRemoteBranch(remote, name);
                    }
                    deleteLater();
                });
        reader_.setWorkspace(repository_);
    }

  private:
    bool current() const {
        return owner_ && git_ && git_->repository() == repository_ &&
               git_->workspaceGeneration() == generation_ && !git_->busy();
    }
    bool confirm(const QString& message) const {
        return current() &&
               ConfirmationDialog::ask(owner_, tr("Confirm Branch Deletion"), message,
                                       tr("Delete Branch"), {},
                                       tr("Cancel")) == ConfirmationChoice::Primary &&
               current();
    }
    QPointer<QWidget> owner_;
    QPointer<workspace::GitService> git_;
    QString repository_;
    quint64 generation_{};
    GitBranchAction action_;
    bool started_{};
    workspace::GitService reader_;
};
} // namespace

void requestGitBranchAction(QWidget* owner, workspace::GitService* git, GitBranchAction action) {
    if (!owner || !git || git->busy() || git->repository().isEmpty())
        return;
    if (action == GitBranchAction::Rename) {
        components::QuickInputDialog input(QObject::tr("Rename Branch"),
                                           QObject::tr("New branch name"), {}, owner);
        if (input.exec() == QDialog::Accepted)
            git->renameCurrentBranch(input.value());
        return;
    }
    new BranchRequest(owner, git, action);
}
} // namespace litecode::ui
