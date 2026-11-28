#include "ui/GitRepositoryPicker.h"
#include "ui/ConfirmationDialog.h"
#include "ui/QuickOpenDialog.h"
#include "workspace/GitRepositories.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
namespace litecode::ui {
void requestGitRepository(QWidget* owner, const QString& workspaceRoot, const QString& current,
                          const std::function<void(const QString&)>& selected) {
    auto* watcher = new QFutureWatcher<QStringList>(owner);
    QObject::connect(
        watcher, &QFutureWatcher<QStringList>::finished, owner,
        [watcher, owner, current, selected] {
            auto repositories = watcher->result();
            watcher->deleteLater();
            if (!current.isEmpty() && !repositories.contains(current))
                repositories.prepend(current);
            if (repositories.isEmpty()) {
                ConfirmationDialog::showMessage(
                    owner, QObject::tr("Repositories"),
                    QObject::tr("No repositories found under this workspace."));
                return;
            }
            QVector<QuickPickItem> items;
            for (const auto& repository : repositories)
                items.append({repository, QFileInfo(repository).fileName(),
                              repository == current
                                  ? QObject::tr("Current repository — %1").arg(repository)
                                  : repository});
            QuickOpenDialog picker(QObject::tr("Select Repository"),
                                   QObject::tr("Repository name or path"), items, current, owner);
            if (picker.exec() == QDialog::Accepted)
                selected(picker.selectedItemId());
        });
    watcher->setFuture(QtConcurrent::run(
        [workspaceRoot] { return workspace::discoverGitRepositories(workspaceRoot); }));
}
} // namespace litecode::ui
