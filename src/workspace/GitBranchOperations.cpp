#include "workspace/GitService.h"
#include <QRegularExpression>

namespace litecode::workspace {
namespace {
bool branchNameSafe(const QString& name) {
    return !name.isEmpty() && !name.startsWith('-') && !name.contains(QChar('\0')) &&
           !name.contains('\n') && !name.contains('\r') && !name.contains("@{");
}
bool remoteNameSafe(const QString& name) {
    static const QRegularExpression pattern("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    return pattern.match(name).hasMatch();
}
bool mergeRefSafe(const QString& ref, bool rebase) {
    if (ref.contains(QChar('\0')) || ref.contains('\n') || ref.contains('\r'))
        return false;
    return ref.startsWith("refs/heads/") || ref.startsWith("refs/remotes/") ||
           (!rebase && ref.startsWith("refs/tags/"));
}
} // namespace

void GitService::mergeBranch(const QString& ref) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!mergeRefSafe(ref, false)) {
        emit error(tr("Select a branch or tag to merge."));
        return;
    }
    mutate({"merge", "--no-edit", "--", ref});
}
void GitService::rebaseBranch(const QString& ref) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!mergeRefSafe(ref, true)) {
        emit error(tr("Select a branch to rebase onto."));
        return;
    }
    mutate({"rebase", "--", ref});
}
void GitService::renameCurrentBranch(const QString& name) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!branchNameSafe(name)) {
        emit error(tr("Enter a valid branch name."));
        return;
    }
    run(repository_, {"check-ref-format", "--branch", name},
        [this, name](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0)
                emit error(diagnostic.isEmpty() ? tr("Invalid branch name.") : diagnostic);
            else
                mutate({"branch", "-m", name});
        });
}
void GitService::deleteBranch(const QString& name) {
    if (!branchNameSafe(name)) {
        emit error(tr("Select a valid local branch."));
        return;
    }
    mutate({"branch", "-d", "--", name}); // Git refuses unmerged/current branches.
}
void GitService::deleteRemoteBranch(const QString& remote, const QString& name) {
    if (!remoteNameSafe(remote) || !branchNameSafe(name)) {
        emit error(tr("Select a remote branch."));
        return;
    }
    mutate({"push", remote, ":refs/heads/" + name});
}
} // namespace litecode::workspace
