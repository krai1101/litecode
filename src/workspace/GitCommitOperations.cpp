#include "workspace/GitService.h"

namespace litecode::workspace {
void GitService::commitWithOptions(const QString& message, GitCommitOptions options) {
    if (busy_ || repository_.isEmpty())
        return;
    if (options.noVerify && !preferences_.allowNoVerifyCommit) {
        emit error(tr("Enable No Verify commits in Git Settings first."));
        return;
    }
    if (message.trimmed().isEmpty() && !options.amend) {
        emit error(tr("Enter a commit message."));
        return;
    }
    bool staged = false, unstaged = false;
    for (const auto& change : status_.changes) {
        if (change.conflict) {
            emit error(tr("Resolve and stage merge conflicts before committing."));
            return;
        }
        staged |= change.index != ' ' && change.index != '?';
        unstaged |= change.worktree != ' ' || change.index == '?';
    }
    if (!options.amend && !staged && (options.scope == GitCommitScope::Staged || !unstaged)) {
        emit error(tr("Stage changes before committing, or choose Commit All."));
        return;
    }
    QStringList arguments{"commit"};
    if (options.amend)
        arguments.append("--amend");
    if (options.signoff)
        arguments.append("--signoff");
    if (options.noVerify)
        arguments.append("--no-verify");
    if (message.trimmed().isEmpty())
        arguments.append("--no-edit");
    else
        arguments.append({"-m", message});
    if (options.scope == GitCommitScope::Staged || !unstaged) {
        mutate(arguments);
        return;
    }
    run(repository_, {"add", "-A"},
        [this, arguments](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Could not stage all changes.") : diagnostic);
                emit operationFinished();
                refresh();
            } else
                mutate(arguments);
        });
}

void GitService::undoCommit() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_, {"show", "-s", "--format=%B", "HEAD"},
        [this](int code, const QByteArray& output, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            const auto message = QString::fromUtf8(output).trimmed();
            run(repository_, {"rev-list", "--parents", "-n", "1", "HEAD"},
                [this, message](int result, const QByteArray& parents, const QString& errorText) {
                    if (result != 0) {
                        emit error(errorText);
                        return;
                    }
                    const auto count = QString::fromUtf8(parents).trimmed().split(' ').size();
                    if (count > 1)
                        run(repository_, {"reset", "--mixed", "HEAD~1"},
                            [this, message](int resetCode, const QByteArray&,
                                            const QString& error) {
                                if (resetCode != 0)
                                    emit this->error(error);
                                else
                                    emit commitUndone(message);
                                emit operationFinished();
                                refresh();
                            });
                    else
                        run(repository_, {"update-ref", "-d", "HEAD"},
                            [this, message](int deleteCode, const QByteArray&,
                                            const QString& error) {
                                if (deleteCode != 0) {
                                    emit this->error(error);
                                    return;
                                }
                                run(repository_, {"read-tree", "--empty"},
                                    [this, message](int clearCode, const QByteArray&,
                                                    const QString& diagnostic) {
                                        if (clearCode != 0)
                                            emit this->error(diagnostic);
                                        else
                                            emit commitUndone(message);
                                        emit operationFinished();
                                        refresh();
                                    });
                            });
                });
        });
}
void GitService::abortRebase() { mutate({"rebase", "--abort"}); }
} // namespace litecode::workspace
