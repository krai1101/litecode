#include "workspace/GitService.h"
#include <QRegularExpression>

namespace litecode::workspace {
namespace {
bool validRemote(const QString& remote) {
    static const QRegularExpression pattern(QStringLiteral("^[a-zA-Z0-9_][a-zA-Z0-9_.-]*$"));
    return pattern.match(remote).hasMatch();
}
} // namespace

void GitService::pull() { pullWithMode(std::nullopt, false); }

void GitService::pullRebase() { pullWithMode(true, false); }

void GitService::pullFrom(const QString& remote, const QString& branch) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!validRemote(remote) || branch.isEmpty() || branch.startsWith('-') ||
        branch.contains(QChar('\0')) || branch.contains(QChar('\n')) ||
        branch.contains(QChar('\r'))) {
        emit error(tr("Select a valid remote branch to pull from."));
        return;
    }
    run(repository_, {"check-ref-format", "--branch", branch},
        [this, remote, branch](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Invalid remote branch name.") : diagnostic);
                return;
            }
            pullWithMode(false, false, remote, branch);
        });
}

void GitService::pushTo(const QString& remote) { pushToWithMode(remote, false); }

void GitService::pushToForce(const QString& remote) { pushToWithMode(remote, true); }

void GitService::pushToWithMode(const QString& remote, bool force) {
    if (busy_ || repository_.isEmpty())
        return;
    if (force && !preferences_.allowForcePush) {
        emit error(tr("Force push is not allowed. Enable Allow Force Push in Git Settings."));
        return;
    }
    if (!validRemote(remote)) {
        emit error(tr("Select a valid remote to push to."));
        return;
    }
    run(repository_, {"symbolic-ref", "--quiet", "--short", "HEAD"},
        [this, remote, force](int code, const QByteArray& output, const QString& diagnostic) {
            const QString branch = QString::fromUtf8(output).trimmed();
            if (code != 0 || branch.isEmpty()) {
                emit error(diagnostic.isEmpty() ? tr("Check out a branch before pushing.")
                                                : diagnostic);
                return;
            }
            QStringList arguments{"push"};
            if (force) {
                if (preferences_.useForcePushWithLease) {
                    arguments.append("--force-with-lease");
                    if (preferences_.useForcePushIfIncludes)
                        arguments.append("--force-if-includes");
                } else {
                    arguments.append("--force");
                }
            }
            arguments.append({remote, "HEAD:refs/heads/" + branch});
            mutate(arguments);
        });
}

void GitService::push(const QString& remote, bool setUpstream) {
    pushWithOptions(remote, setUpstream, false);
}

void GitService::pushForce() { pushWithOptions({}, false, true); }

void GitService::pushWithOptions(const QString& remote, bool setUpstream, bool force) {
    if (busy_ || repository_.isEmpty())
        return;
    if (force && !preferences_.allowForcePush) {
        emit error(tr("Force push is not allowed. Enable Allow Force Push in Git Settings."));
        return;
    }
    if (!remote.isEmpty() && !validRemote(remote)) {
        emit error(tr("Select a valid remote."));
        return;
    }
    if (setUpstream && remote.isEmpty()) {
        emit error(tr("Select a remote to publish this branch."));
        return;
    }
    const auto forceFlags = [this, force] {
        QStringList flags;
        if (force) {
            if (preferences_.useForcePushWithLease) {
                flags.append("--force-with-lease");
                if (preferences_.useForcePushIfIncludes)
                    flags.append("--force-if-includes");
            } else {
                flags.append("--force");
            }
        }
        return flags;
    };
    if (remote.isEmpty() && !setUpstream) {
        run(repository_,
            {"for-each-ref",
             "--format=%(HEAD)%00%(refname:short)%00%(upstream:remotename)%00%(upstream:remoteref)",
             "refs/heads/"},
            [this, forceFlags](int code, const QByteArray& bytes, const QString& diagnostic) {
                if (code != 0) {
                    emit error(diagnostic);
                    return;
                }
                for (const auto& row : bytes.split('\n')) {
                    const auto fields = row.split('\0');
                    if (fields.size() != 4 || fields[0] != "*")
                        continue;
                    if (fields[2].isEmpty() || fields[3].isEmpty()) {
                        emit publishRequired();
                        return;
                    }
                    QStringList arguments{"push"};
                    arguments.append(forceFlags());
                    arguments.append({QString::fromUtf8(fields[2]),
                                      QString::fromUtf8(fields[1]) + ":" +
                                          QString::fromUtf8(fields[3])});
                    mutate(arguments);
                    return;
                }
                emit error(tr("Check out a branch before pushing to a remote."));
            });
        return;
    }
    QStringList arguments{"push"};
    arguments.append(forceFlags());
    if (setUpstream)
        arguments.append("--set-upstream");
    if (!remote.isEmpty())
        arguments.append(remote);
    if (setUpstream)
        arguments.append("HEAD");
    mutate(arguments);
}

void GitService::sync(bool rebase) {
    if (busy_ || repository_.isEmpty())
        return;
    // VS Code publishes instead of pulling when the current branch has no upstream.
    run(repository_, {"rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}"},
        [this, rebase](int code, const QByteArray&, const QString&) {
            if (code != 0) {
                emit publishRequired();
                return;
            }
            pullWithMode(rebase, true);
        });
}

void GitService::pullWithMode(std::optional<bool> rebase, bool pushAfter,
                              const QString& remote, const QString& branch) {
    if (busy_ || repository_.isEmpty())
        return;
    QStringList arguments{"-c", "core.editor=true", "pull", "--no-edit"};
    if (rebase.has_value())
        arguments.append(*rebase ? "--rebase" : "--no-rebase");
    else
        switch (preferences_.pullPolicy) {
        case core::GitPullPolicy::Merge: arguments.append("--no-rebase"); break;
        case core::GitPullPolicy::Rebase: arguments.append("--rebase"); break;
        case core::GitPullPolicy::FastForwardOnly: arguments.append("--ff-only"); break;
        case core::GitPullPolicy::Repository: break;
        }
    if (preferences_.pullTags)
        arguments.append("--tags");
    if (preferences_.autoStash)
        arguments.append("--autostash");
    if (!remote.isEmpty())
        arguments.append({remote, branch});

    const auto finishPull = [this, pushAfter](int code, const QByteArray& output,
                                               const QString& diagnostic) {
        if (code != 0) {
            emit error(diagnostic.isEmpty() ? tr("Pull failed.") : diagnostic);
            if (diagnostic.contains("would clobber existing tag"))
                emit tagConflict(repository_, diagnostic);
        } else if ((QString::fromUtf8(output) + diagnostic)
                       .contains("autostash resulted in conflicts", Qt::CaseInsensitive)) {
            emit error(tr("Pull completed, but restoring your local changes caused conflicts. "
                          "Git retained the auto-stash; resolve the conflicts before pushing.\n%1")
                           .arg(diagnostic));
        } else if (pushAfter) {
            push();
            return;
        } else {
            emit operationFinished();
            refresh();
            return;
        }
        emit operationFinished();
        refresh();
    };
    if (!preferences_.fetchOnPull) {
        run(repository_, arguments, finishPull);
        return;
    }
    QStringList fetchArguments{"fetch", "--all"};
    if (preferences_.pruneOnFetch)
        fetchArguments.append("--prune");
    run(repository_, fetchArguments,
        [this, arguments, finishPull](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Fetch before pull failed.") : diagnostic);
                if (diagnostic.contains("would clobber existing tag"))
                    emit tagConflict(repository_, diagnostic);
                emit operationFinished();
                refresh();
            } else {
                run(repository_, arguments, finishPull);
            }
        });
}
} // namespace litecode::workspace
