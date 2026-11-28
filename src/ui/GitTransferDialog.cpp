#include "ui/GitTransferDialog.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include "workspace/GitService.h"
#include <QPointer>

namespace litecode::ui {
namespace {
class TransferRequest final : public QObject {
  public:
    TransferRequest(QWidget* owner, workspace::GitService* git, GitTransferAction action,
                    bool confirmForcePush)
        : QObject(owner), owner_(owner), git_(git), repository_(git->repository()),
          generation_(git->workspaceGeneration()), action_(action),
          confirmForcePush_(confirmForcePush) {
        connect(&reader_, &workspace::GitService::changed, this, [this] {
            if (!started_ && !reader_.repository().isEmpty()) {
                started_ = true;
                reader_.listRemotes();
            }
        });
        connect(&reader_, &workspace::GitService::error, this, [this](const QString& diagnostic) {
            if (owner_)
                ConfirmationDialog::showMessage(owner_, tr("Git Transfer"), diagnostic);
            deleteLater();
        });
        connect(&reader_, &workspace::GitService::remotesReady, this,
                [this](const QVector<workspace::GitRemote>& remotes) {
                    if (!current()) {
                        deleteLater();
                        return;
                    }
                    QVector<QuickPickItem> items;
                    for (const auto& remote : remotes) {
                        const QString url = action_ == GitTransferAction::PullFrom ? remote.fetchUrl
                                                                                   : remote.pushUrl;
                        if (!url.isEmpty())
                            items.append({remote.name, remote.name, url});
                    }
                    if (items.isEmpty()) {
                        ConfirmationDialog::showMessage(owner_, tr("Git Transfer"),
                                                        tr("No remotes are configured."));
                        deleteLater();
                        return;
                    }
                    // VS Code skips the first picker for Pull From with one remote.
                    if (action_ == GitTransferAction::PullFrom && items.size() == 1)
                        remote_ = items.first().id;
                    else
                        remote_ = pick(action_ == GitTransferAction::PullFrom ? tr("Pull From")
                                                                              : tr("Push To"),
                                       action_ == GitTransferAction::PullFrom
                                           ? tr("Pick a remote to pull the branch from")
                                           : tr("Pick a remote to publish this branch to"),
                                       items);
                    if (remote_.isEmpty()) {
                        deleteLater();
                        return;
                    }
                    if (action_ != GitTransferAction::PullFrom) {
                        if (action_ == GitTransferAction::PushToForce) {
                            if (confirmForcePush_ &&
                                ConfirmationDialog::ask(owner_, tr("Force Push"),
                                                        tr("Force pushing can overwrite commits on "
                                                           "the remote branch. Continue?"),
                                                        tr("Force Push"), {}, tr("Cancel")) !=
                                    ConfirmationChoice::Primary) {
                                deleteLater();
                                return;
                            }
                            if (!current()) {
                                deleteLater();
                                return;
                            }
                            git_->pushToForce(remote_);
                        } else
                            git_->pushTo(remote_);
                        deleteLater();
                    } else {
                        reader_.listBranches();
                    }
                });
        connect(&reader_, &workspace::GitService::branchesReady, this,
                [this](const QVector<workspace::GitBranch>& branches) {
                    if (!current()) {
                        deleteLater();
                        return;
                    }
                    const QString prefix = QStringLiteral("refs/remotes/") + remote_ + '/';
                    QVector<QuickPickItem> items;
                    for (const auto& branch : branches)
                        if (branch.remote && branch.ref.startsWith(prefix) &&
                            !branch.ref.endsWith(QStringLiteral("/HEAD"))) {
                            const QString name = branch.ref.mid(prefix.size());
                            items.append({name, name, branch.commit.left(7)});
                        }
                    const QString branch =
                        pick(tr("Pull From"), tr("Pick a branch to pull from"), items);
                    if (!branch.isEmpty() && current())
                        git_->pullFrom(remote_, branch);
                    deleteLater();
                });
        reader_.setWorkspace(repository_);
    }

  private:
    bool current() const {
        return owner_ && git_ && git_->repository() == repository_ &&
               git_->workspaceGeneration() == generation_ && !git_->busy();
    }
    QString pick(const QString& title, const QString& prompt, const QVector<QuickPickItem>& items) {
        if (!current())
            return {};
        if (items.isEmpty()) {
            ConfirmationDialog::showMessage(owner_, title, tr("No matching branches or remotes."));
            return {};
        }
        QuickOpenDialog picker(title, prompt, items, {}, owner_);
        return picker.exec() == QDialog::Accepted && current() ? picker.selectedItemId()
                                                               : QString{};
    }
    QPointer<QWidget> owner_;
    QPointer<workspace::GitService> git_;
    QString repository_, remote_;
    quint64 generation_{};
    GitTransferAction action_;
    bool confirmForcePush_{};
    bool started_{};
    workspace::GitService reader_;
};
} // namespace

void requestGitTransfer(QWidget* owner, workspace::GitService* git, GitTransferAction action,
                        bool confirmForcePush) {
    if (owner && git && !git->busy() && !git->repository().isEmpty())
        new TransferRequest(owner, git, action, confirmForcePush);
}
} // namespace litecode::ui
