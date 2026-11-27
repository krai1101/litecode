#include "workspace/GitService.h"
#include <QRegularExpression>

namespace litecode::workspace {
QString GitService::sanitizeBranchName(const QString& name, const QString& whitespace) {
    auto result = name.trimmed();
    result.remove(QRegularExpression("^-+"));
    // Same sanitization expression used by VS Code's Git commands.ts. Git is
    // still the final authority; sanitization is displayed before acceptance.
    result.replace(
        QRegularExpression(R"(^\.|/\.|\.\.|~|\^|:|/$|\.lock$|\.lock/|\\|\*|\s|^\s*$|\.$|\[|\]$)"),
        whitespace);
    return result;
}
void GitService::validateBranchName(const QString& name) {
    if (busy_ || repository_.isEmpty())
        return;
    if (name.isEmpty() || name.startsWith('-') || name.contains("@{") ||
        name.contains(QChar('\0')) || name.contains('\n') || name.contains('\r')) {
        emit branchNameValidated(name, tr("Enter a valid branch name."));
        return;
    }
    run(repository_, {"check-ref-format", "--branch", name},
        [this, name](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit branchNameValidated(name, diagnostic.isEmpty() ? tr("Invalid branch name.")
                                                                    : diagnostic);
                return;
            }
            run(repository_, {"show-ref", "--verify", "--quiet", "refs/heads/" + name},
                [this, name](int result, const QByteArray&, const QString& message) {
                    emit branchNameValidated(
                        name, result == 0         ? tr("Branch '%1' already exists.").arg(name)
                              : result == 1       ? QString()
                              : message.isEmpty() ? tr("Unable to validate branch name.")
                                                  : message);
                });
        });
}
void GitService::checkoutDetached(const QString& ref, GitCheckoutRecovery recovery) {
    if (busy_ || repository_.isEmpty())
        return;
    if (ref.isEmpty() || ref.startsWith('-') || ref.contains(QChar('\0')) || ref.contains('\n') ||
        ref.contains('\r')) {
        emit error(tr("Select a valid revision."));
        return;
    }
    run(repository_, {"rev-parse", "--verify", "--end-of-options", ref + "^{commit}"},
        [this, ref, recovery](int code, const QByteArray& bytes, const QString& diagnostic) {
            if (code != 0)
                emit error(diagnostic);
            else
                checkoutCommand({"switch", "--detach", "--", QString::fromUtf8(bytes).trimmed()},
                                ref, true, recovery);
        });
}
} // namespace litecode::workspace
