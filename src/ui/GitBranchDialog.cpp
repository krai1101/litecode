#include "ui/GitBranchDialog.h"
#include "ui/components/QuickInputDialog.h"
#include "workspace/GitService.h"
#include <QLineEdit>
#include <QTimer>
#include <QToolButton>
#include <QUuid>

namespace litecode::ui {
std::optional<QString> requestGitBranchName(QWidget* owner, const QString& repository,
                                            const core::GitPreferences& preferences) {
    components::QuickInputDialog dialog(QObject::tr("Create Branch"), QObject::tr("Branch name"),
                                        {}, owner);
    dialog.setValidation({}, true);
    workspace::GitService validator(&dialog);
    QTimer debounce(&dialog);
    debounce.setSingleShot(true);
    debounce.setInterval(100);
    const auto candidate = [&] {
        return workspace::GitService::sanitizeBranchName(dialog.value(),
                                                         preferences.branchWhitespace);
    };
    const auto validate = [&] {
        dialog.setValidation({}, true);
        if (validator.busy()) {
            debounce.start();
            return;
        }
        if (validator.repository().isEmpty()) {
            dialog.setValidation(QObject::tr("Git repository is no longer available."));
            return;
        }
        const auto name = candidate();
        if (name.isEmpty()) {
            dialog.setValidation(QObject::tr("Enter a branch name."));
            return;
        }
        validator.validateBranchName(name);
    };
    QObject::connect(&debounce, &QTimer::timeout, &dialog, validate);
    QObject::connect(dialog.input(), &QLineEdit::textChanged, &dialog, [&] {
        dialog.setValidation({}, true);
        debounce.start();
    });
    QObject::connect(
        &validator, &workspace::GitService::branchNameValidated, &dialog,
        [&](const QString& name, const QString& diagnostic) {
            if (name != candidate()) {
                debounce.start();
                return;
            }
            dialog.setValidation(diagnostic);
            dialog.setHint(
                name == dialog.value()
                    ? QObject::tr("Enter to create branch; Escape to cancel")
                    : QObject::tr(
                          "The new branch will be '%1'. Enter to confirm; Escape to cancel.")
                          .arg(name));
        });
    QObject::connect(&validator, &workspace::GitService::error, &dialog,
                     [&](const QString& diagnostic) {
                         debounce.stop();
                         dialog.setValidation(diagnostic);
                     });
    QObject::connect(&validator, &workspace::GitService::changed, &dialog, [&] {
        if (!validator.repository().isEmpty())
            debounce.start();
        else if (!validator.busy())
            dialog.setValidation(QObject::tr("Git repository is no longer available."));
    });
    if (preferences.randomBranchName) {
        const auto generate = [&] {
            dialog.input()->setText(preferences.branchPrefix + "branch-" +
                                    QUuid::createUuid().toString(QUuid::Id128).left(8));
        };
        auto* refresh =
            dialog.addInputAction(":/icons/refresh.svg", QObject::tr("Generate a new branch name"));
        QObject::connect(refresh, &QToolButton::clicked, &dialog, generate);
        generate();
    } else {
        dialog.input()->setText(preferences.branchPrefix);
    }
    validator.setWorkspace(repository);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return candidate();
}
} // namespace litecode::ui
