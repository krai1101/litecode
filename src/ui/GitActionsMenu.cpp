#include "ui/GitActionsMenu.h"
#include "ui/components/Controls.h"
#include <QAction>
#include <algorithm>
namespace litecode::ui {
void groupGitActionsMenu(components::Menu& menu, QAction* views, QAction* repositories,
                         const QList<QAction*>& quickActions, const QList<GitMenuGroup>& groups) {
    // QMenu::clear would delete the actions owned by this menu. Removing them
    // keeps the original action instances alive for their new submenu surfaces.
    for (auto* action : menu.actions())
        menu.removeAction(action);
    menu.addAction(views);
    menu.addAction(repositories);
    menu.addSeparator();
    for (auto* action : quickActions)
        menu.addAction(action);
    menu.addSeparator();
    for (const auto& group : groups) {
        auto* submenu = new components::Menu(group.title, &menu);
        submenu->setMinimumWidth(240);
        for (auto* action : group.actions) {
            if (action)
                submenu->addAction(action);
            else
                submenu->addSeparator();
        }
        submenu->setEnabled(std::any_of(group.actions.cbegin(), group.actions.cend(),
                                        [](const QAction* action) {
                                            return action && action->isEnabled();
                                        }));
        menu.addMenu(submenu);
    }
}
} // namespace litecode::ui
