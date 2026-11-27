#include "ui/GitTagRecoveryDialog.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include "workspace/GitService.h"
#include <QPointer>
#include <QTimer>
namespace litecode::ui {
namespace {
class TagRecoveryRequest final : public QObject {
  public:
    TagRecoveryRequest(QWidget* owner, workspace::GitService* git, bool restore)
        : QObject(owner), owner_(owner), git_(git), repository_(git->repository()),
          restore_(restore), generation_(git->workspaceGeneration()) {
        connect(&reader_, &workspace::GitService::changed, this, [this] {
            if (started_ || reader_.repository().isEmpty())
                return;
            started_ = true;
            if (restore_)
                reader_.listTagBackups();
            else
                reader_.listBranches();
        });
        connect(&reader_, &workspace::GitService::error, this, [this](const QString& diagnostic) {
            ConfirmationDialog::showMessage(owner_, tr("Tag Recovery"), diagnostic);
            deleteLater();
        });
        connect(&reader_, &workspace::GitService::tagBackupsReady, this,
                [this](const QStringList& refs) {
                    QVector<QuickPickItem> items;
                    for (const auto& ref : refs)
                        items.append({ref, ref.section('/', 5), ref.section('/', 4, 4)});
                    selectTag(items);
                });
        connect(&reader_, &workspace::GitService::branchesReady, this,
                [this](const QVector<workspace::GitBranch>& branches) {
                    QVector<QuickPickItem> items;
                    for (const auto& branch : branches)
                        if (branch.tag)
                            items.append({branch.ref, branch.name, branch.commit.left(7)});
                    selectTag(items);
                });
        connect(&reader_, &workspace::GitService::remotesReady, this,
                [this](const QVector<workspace::GitRemote>& remotes) {
                    QVector<QuickPickItem> items;
                    for (const auto& remote : remotes)
                        items.append({remote.name, remote.name, remote.fetchUrl});
                    if (items.isEmpty()) {
                        finishWithMessage(tr("No remotes configured."));
                        return;
                    }
                    QuickOpenDialog picker(tr("Replace Conflicting Tag"), tr("Select the remote"),
                                           items, {}, owner_);
                    if (picker.exec() != QDialog::Accepted || !current()) {
                        deleteLater();
                        return;
                    }
                    remote_ = picker.selectedItemId();
                    confirm();
                });
        ready_.setInterval(50);
        connect(&ready_, &QTimer::timeout, this, [this] {
            if (!current()) {
                ready_.stop();
                deleteLater();
                return;
            }
            if (git_->busy())
                return;
            ready_.stop();
            if (restore_)
                git_->restoreTagBackup(tag_);
            else
                git_->replaceRemoteTag(remote_, tag_);
            deleteLater();
        });
        reader_.setWorkspace(repository_);
    }

  private:
    bool current() const {
        return owner_ && git_ && git_->repository() == repository_ &&
               git_->workspaceGeneration() == generation_;
    }
    void finishWithMessage(const QString& text) {
        ConfirmationDialog::showMessage(owner_, tr("Tag Recovery"), text);
        deleteLater();
    }
    void selectTag(const QVector<QuickPickItem>& items) {
        if (!current()) {
            deleteLater();
            return;
        }
        if (items.isEmpty()) {
            finishWithMessage(tr("No matching tags or tag backups."));
            return;
        }
        QuickOpenDialog picker(tr("Tag Recovery"),
                               restore_ ? tr("Select a tag backup")
                                        : tr("Select the conflicting tag"),
                               items, {}, owner_);
        if (picker.exec() != QDialog::Accepted || !current()) {
            deleteLater();
            return;
        }
        tag_ = picker.selectedItemId();
        if (restore_)
            confirm();
        else
            reader_.listRemotes();
    }
    void confirm() {
        if (!current()) {
            deleteLater();
            return;
        }
        const auto message =
            restore_
                ? tr("Restore tag '%1' from this retained backup? The backup will not be deleted.")
                      .arg(tag_.section('/', 5))
                : tr("Replace local tag '%1' with the version from '%2'? The exact original tag "
                     "object will first be preserved in a recovery ref. This does not push or "
                     "change working files.")
                      .arg(tag_.mid(10), remote_);
        if (ConfirmationDialog::ask(owner_, tr("Confirm Tag Recovery"), message,
                                    restore_ ? tr("Restore Tag") : tr("Back Up and Replace"), {},
                                    tr("Cancel")) != ConfirmationChoice::Primary) {
            deleteLater();
            return;
        }
        ready_.start();
    }
    QPointer<QWidget> owner_;
    QPointer<workspace::GitService> git_;
    QString repository_, tag_, remote_;
    bool restore_{}, started_{};
    quint64 generation_{};
    workspace::GitService reader_;
    QTimer ready_;
};
} // namespace
void requestGitTagRecovery(QWidget* owner, workspace::GitService* git, bool restore) {
    if (git->repository().isEmpty())
        return;
    new TagRecoveryRequest(owner, git, restore);
}
} // namespace litecode::ui
