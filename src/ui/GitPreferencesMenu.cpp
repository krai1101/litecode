#include "ui/GitPreferencesMenu.h"
#include "ui/ConfirmationDialog.h"
#include "ui/components/Controls.h"
#include "ui/components/QuickInputDialog.h"
#include <QActionGroup>
namespace litecode::ui {
void addGitPreferencesMenu(components::Menu& menu, core::GitPreferences& preferences,
                           const std::function<void()>& changed, QWidget* owner) {
    auto* settings = new components::Menu(QObject::tr("Git Settings"), &menu);
    settings->setMinimumWidth(280);
    menu.addMenu(settings);
    const auto toggle = [&](const QString& label, bool& value, bool sensitive = false) {
        auto* action = settings->addAction(label);
        action->setCheckable(true);
        action->setChecked(value);
        QObject::connect(action, &QAction::triggered, settings,
                         [&, changed, owner, action, sensitive, field = &value](bool checked) {
                             if (sensitive && checked &&
                                 ConfirmationDialog::ask(
                                     owner, QObject::tr("Automatic Stashing"),
                                     QObject::tr("Allow Git to stash tracked changes temporarily "
                                                 "while pulling, then reapply them? "
                                                 "Reapplication may produce conflicts; Git keeps "
                                                 "recovery stashes when necessary."),
                                     QObject::tr("Enable"), {},
                                     QObject::tr("Cancel")) != ConfirmationChoice::Primary) {
                                 action->setChecked(false);
                                 return;
                             }
                             *field = checked;
                             changed();
                         });
    };
    toggle(QObject::tr("Prune on Fetch"), preferences.pruneOnFetch);
    toggle(QObject::tr("Fetch All Before Pull"), preferences.fetchOnPull);
    toggle(QObject::tr("Fetch Tags on Pull"), preferences.pullTags);
    toggle(QObject::tr("Auto Stash on Pull"), preferences.autoStash, true);
    toggle(QObject::tr("Allow Commits Without Verification"), preferences.allowNoVerifyCommit);
    auto* allowForce = settings->addAction(QObject::tr("Allow Force Push"));
    allowForce->setCheckable(true);
    allowForce->setChecked(preferences.allowForcePush);
    QObject::connect(
        allowForce, &QAction::triggered, settings,
        [changed, owner, allowForce, field = &preferences.allowForcePush](bool checked) {
            if (checked &&
                ConfirmationDialog::ask(owner, QObject::tr("Enable Force Push"),
                                        QObject::tr("Force pushing can overwrite commits on a "
                                                    "remote branch. Allow Force Push commands?"),
                                        QObject::tr("Enable"), {},
                                        QObject::tr("Cancel")) != ConfirmationChoice::Primary) {
                allowForce->setChecked(false);
                return;
            }
            *field = checked;
            changed();
        });
    toggle(QObject::tr("Use Force Push With Lease"), preferences.useForcePushWithLease);
    toggle(QObject::tr("Use Force Push If Includes"), preferences.useForcePushIfIncludes);
    toggle(QObject::tr("Confirm Force Push"), preferences.confirmForcePush);
    auto* policy = new components::Menu(QObject::tr("Pull Policy"), settings);
    policy->setMinimumWidth(240);
    settings->addMenu(policy);
    auto* group = new QActionGroup(policy);
    const QStringList labels{QObject::tr("Repository Configuration"), QObject::tr("Merge"),
                             QObject::tr("Rebase"), QObject::tr("Fast-forward Only")};
    for (int i = 0; i < labels.size(); ++i) {
        auto* action = policy->addAction(labels[i]);
        group->addAction(action);
        action->setCheckable(true);
        action->setChecked(static_cast<int>(preferences.pullPolicy) == i);
        QObject::connect(action, &QAction::triggered, policy, [&, i, changed] {
            preferences.pullPolicy = static_cast<core::GitPullPolicy>(i);
            changed();
        });
    }
    settings->addSeparator();
    toggle(QObject::tr("Generate Random Branch Names"), preferences.randomBranchName);
    settings->addAction(QObject::tr("Set Branch Prefix…"), owner, [&, owner, changed] {
        components::QuickInputDialog input(QObject::tr("Branch Prefix"),
                                           QObject::tr("Branch prefix"), {}, owner);
        if (input.exec() == QDialog::Accepted) {
            preferences.branchPrefix = input.value();
            changed();
        }
    });
    settings->addAction(QObject::tr("Clear Branch Prefix"), owner, [&, changed] {
        preferences.branchPrefix.clear();
        changed();
    });
    settings->addAction(QObject::tr("Branch Whitespace Replacement…"), owner, [&, owner, changed] {
        components::QuickInputDialog input(QObject::tr("Branch Names"),
                                           QObject::tr("Whitespace replacement"), "-", owner);
        if (input.exec() == QDialog::Accepted) {
            preferences.branchWhitespace = input.value();
            changed();
        }
    });
}
} // namespace litecode::ui
