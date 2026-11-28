#include "ui/GitTagDialog.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include "ui/components/QuickInputDialog.h"
#include "workspace/GitService.h"
#include <QPointer>

namespace litecode::ui {
namespace {
class TagRequest final : public QObject {
  public:
    TagRequest(QWidget* owner, workspace::GitService* git, GitTagAction action)
        : QObject(owner), owner_(owner), git_(git), repository_(git->repository()),
          generation_(git->workspaceGeneration()), action_(action) {
        connect(&reader_, &workspace::GitService::changed, this, [this] {
            if (started_ || reader_.repository().isEmpty())
                return;
            started_ = true;
            if (action_ == GitTagAction::DeleteLocal)
                reader_.listBranches();
            else
                reader_.listRemotes();
        });
        connect(&reader_, &workspace::GitService::error, this, [this](const QString& message) {
            ConfirmationDialog::showMessage(owner_, tr("Tags"), message);
            deleteLater();
        });
        connect(&reader_, &workspace::GitService::branchesReady, this,
                [this](const QVector<workspace::GitBranch>& branches) {
                    QVector<QuickPickItem> items;
                    for (const auto& branch : branches)
                        if (branch.tag)
                            items.append({branch.name, branch.name, branch.commit.left(7)});
                    const auto name =
                        pick(tr("Delete Tag"), tr("Select a local tag to delete"), items);
                    if (name.isEmpty())
                        return;
                    if (confirm(tr("Delete local tag '%1'? The remote tag is unchanged.").arg(name),
                                tr("Delete Tag")))
                        git_->deleteTag(name);
                    deleteLater();
                });
        connect(
            &reader_, &workspace::GitService::remotesReady, this,
            [this](const QVector<workspace::GitRemote>& remotes) {
                QVector<QuickPickItem> items;
                for (const auto& remote : remotes)
                    if (!remote.pushUrl.isEmpty())
                        items.append({remote.name, remote.name, remote.pushUrl});
                remote_ = pick(tr("Tags"), tr("Select a remote"), items);
                if (remote_.isEmpty())
                    return;
                if (action_ == GitTagAction::Push) {
                    if (confirm(tr("Push all local tags to '%1'?").arg(remote_), tr("Push Tags")))
                        git_->pushTags(remote_);
                    deleteLater();
                } else {
                    reader_.listRemoteTags(remote_);
                }
            });
        connect(&reader_, &workspace::GitService::remoteTagsReady, this,
                [this](const QString& remote, const QStringList& names) {
                    if (remote != remote_ || !current()) {
                        deleteLater();
                        return;
                    }
                    QVector<QuickPickItem> items;
                    for (const auto& name : names)
                        items.append({name, name, remote});
                    const auto name =
                        pick(tr("Delete Remote Tag"),
                             tr("Select a tag to delete from '%1'").arg(remote), items);
                    if (name.isEmpty())
                        return;
                    if (confirm(tr("Delete tag '%1' from remote '%2'? This changes the remote "
                                   "repository, not your local tag.")
                                    .arg(name, remote),
                                tr("Delete Remote Tag")))
                        git_->deleteRemoteTag(remote, name);
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
        if (!current()) {
            deleteLater();
            return {};
        }
        if (items.isEmpty()) {
            ConfirmationDialog::showMessage(owner_, title, tr("No matching tags or remotes."));
            deleteLater();
            return {};
        }
        QuickOpenDialog picker(title, prompt, items, {}, owner_);
        if (picker.exec() != QDialog::Accepted || !current()) {
            deleteLater();
            return {};
        }
        return picker.selectedItemId();
    }
    bool confirm(const QString& message, const QString& button) const {
        return current() &&
               ConfirmationDialog::ask(owner_, tr("Confirm %1").arg(button), message, button, {},
                                       tr("Cancel")) == ConfirmationChoice::Primary &&
               current();
    }
    QPointer<QWidget> owner_;
    QPointer<workspace::GitService> git_;
    QString repository_, remote_;
    quint64 generation_{};
    GitTagAction action_;
    bool started_{};
    workspace::GitService reader_;
};
} // namespace

void requestGitTagAction(QWidget* owner, workspace::GitService* git, GitTagAction action) {
    if (!owner || !git || git->busy() || git->repository().isEmpty())
        return;
    if (action == GitTagAction::Create) {
        components::QuickInputDialog nameInput(QObject::tr("Create Tag"), QObject::tr("Tag name"),
                                               {}, owner);
        if (nameInput.exec() != QDialog::Accepted)
            return;
        const auto name = nameInput.value();
        const auto type = ConfirmationDialog::ask(
            owner, QObject::tr("Create Tag"),
            QObject::tr("Create an annotated tag with a message, or a lightweight tag?"),
            QObject::tr("Annotated"), QObject::tr("Lightweight"), QObject::tr("Cancel"));
        if (type == ConfirmationChoice::Cancel)
            return;
        if (type == ConfirmationChoice::Secondary) {
            git->createTag(name);
            return;
        }
        components::QuickInputDialog messageInput(QObject::tr("Create Annotated Tag"),
                                                  QObject::tr("Tag message"), {}, owner);
        if (messageInput.exec() == QDialog::Accepted)
            git->createTag(name, messageInput.value());
        return;
    }
    new TagRequest(owner, git, action);
}
} // namespace litecode::ui
