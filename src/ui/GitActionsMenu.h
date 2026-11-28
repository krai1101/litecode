#pragma once
#include <QList>
#include <QString>
class QAction;
namespace litecode::ui::components {
class Menu;
}
namespace litecode::ui {
struct GitMenuGroup {
    QString title;
    QList<QAction*> actions;
};
// Reorganizes existing actions without replacing callbacks or changing ownership.
void groupGitActionsMenu(components::Menu& menu, QAction* views, QAction* repositories,
                         const QList<QAction*>& quickActions, const QList<GitMenuGroup>& groups);
} // namespace litecode::ui
