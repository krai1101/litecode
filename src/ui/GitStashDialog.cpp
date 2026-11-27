#include "ui/GitStashDialog.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include "ui/components/Controls.h"
#include "ui/components/QuickInputDialog.h"
#include "workspace/GitService.h"
#include <QDialog>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace litecode::ui {
namespace {
class StashRequest final : public QObject {
  public:
    StashRequest(QWidget* owner, workspace::GitService* git, GitStashAction action)
        : QObject(owner), owner_(owner), git_(git), repository_(git->repository()),
          generation_(git->workspaceGeneration()), action_(action) {
        connect(&reader_, &workspace::GitService::changed, this, [this] {
            if (!started_ && !reader_.repository().isEmpty()) {
                started_ = true;
                reader_.listStashes();
            }
        });
        connect(&reader_, &workspace::GitService::error, this, [this](const QString& message) {
            ConfirmationDialog::showMessage(owner_, tr("Stash"), message);
            deleteLater();
        });
        connect(
            &reader_, &workspace::GitService::stashesReady, this,
            [this](const QStringList& refs, const QStringList& descriptions) {
                if (!current()) {
                    deleteLater();
                    return;
                }
                if (refs.isEmpty()) {
                    ConfirmationDialog::showMessage(owner_, tr("Stash"),
                                                    tr("This repository has no stashes."));
                    deleteLater();
                    return;
                }
                if (action_ == GitStashAction::DropAll) {
                    if (confirm(tr("Drop all %1 stashes? They may be impossible to recover.")
                                    .arg(refs.size()),
                                tr("Drop All Stashes")))
                        git_->dropAllStashes();
                    deleteLater();
                    return;
                }
                QString ref;
                if (action_ == GitStashAction::ApplyLatest || action_ == GitStashAction::PopLatest)
                    ref = refs.first();
                else {
                    QVector<QuickPickItem> items;
                    for (int i = 0; i < refs.size(); ++i)
                        items.append({refs[i], refs[i], descriptions.value(i)});
                    QuickOpenDialog picker(tr("Stash"), tr("Select a stash"), items, {}, owner_);
                    if (picker.exec() != QDialog::Accepted || !current()) {
                        deleteLater();
                        return;
                    }
                    ref = picker.selectedItemId();
                }
                if (action_ == GitStashAction::Apply || action_ == GitStashAction::ApplyLatest)
                    git_->applyStash(ref);
                else if (action_ == GitStashAction::Pop || action_ == GitStashAction::PopLatest)
                    git_->applyStash(ref, true);
                else if (action_ == GitStashAction::Drop) {
                    if (confirm(tr("Drop %1? It may be impossible to recover.").arg(ref),
                                tr("Drop Stash")))
                        git_->dropStash(ref);
                } else if (action_ == GitStashAction::View) {
                    connect(git_, &workspace::GitService::stashPatchReady, this,
                            [this](const QString& shown, const QString& patch) {
                                if (!current()) {
                                    deleteLater();
                                    return;
                                }
                                QDialog dialog(owner_);
                                dialog.setWindowTitle(tr("View Stash: %1").arg(shown));
                                dialog.resize(900, 640);
                                auto* layout = new QVBoxLayout(&dialog);
                                auto* editor = new QPlainTextEdit(&dialog);
                                editor->setReadOnly(true);
                                editor->setPlainText(patch);
                                layout->addWidget(editor);
                                auto* close = new components::Button(tr("Close"), &dialog);
                                layout->addWidget(close);
                                connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
                                dialog.exec();
                                deleteLater();
                            });
                    git_->viewStash(ref);
                    return;
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
    bool confirm(const QString& message, const QString& button) const {
        return current() &&
               ConfirmationDialog::ask(owner_, tr("Confirm %1").arg(button), message, button, {},
                                       tr("Cancel")) == ConfirmationChoice::Primary &&
               current();
    }
    QPointer<QWidget> owner_;
    QPointer<workspace::GitService> git_;
    QString repository_;
    quint64 generation_{};
    GitStashAction action_;
    bool started_{};
    workspace::GitService reader_;
};
} // namespace

void requestGitStashAction(QWidget* owner, workspace::GitService* git, GitStashAction action) {
    if (!owner || !git || git->busy() || git->repository().isEmpty())
        return;
    if (action == GitStashAction::Create || action == GitStashAction::CreateUntracked ||
        action == GitStashAction::CreateStaged) {
        const auto choice = ConfirmationDialog::ask(
            owner, QObject::tr("Stash"), QObject::tr("Add a message to this stash?"),
            QObject::tr("Add Message"), QObject::tr("No Message"), QObject::tr("Cancel"));
        if (choice == ConfirmationChoice::Cancel)
            return;
        QString message;
        if (choice == ConfirmationChoice::Primary) {
            components::QuickInputDialog input(QObject::tr("Stash"), QObject::tr("Stash message"),
                                               {}, owner);
            if (input.exec() != QDialog::Accepted)
                return;
            message = input.value();
        }
        git->stash(action == GitStashAction::CreateUntracked,
                   action == GitStashAction::CreateStaged, message);
        return;
    }
    new StashRequest(owner, git, action);
}
} // namespace litecode::ui
