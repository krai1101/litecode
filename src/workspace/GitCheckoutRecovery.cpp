#include "workspace/GitService.h"
#include <QRegularExpression>
#include <QUuid>
#include <memory>

namespace litecode::workspace {
namespace {
bool recoveryRef(const QString& ref) {
    static const QRegularExpression pattern("^refs/litecode/recovery/checkouts/[a-f0-9]{32}$");
    return pattern.match(ref).hasMatch();
}
} // namespace
void GitService::checkoutCommand(const QStringList& arguments, const QString& ref, bool detached,
                                 GitCheckoutRecovery recovery) {
    if (recovery == GitCheckoutRecovery::None) {
        run(repository_, arguments,
            [this, ref, detached](int code, const QByteArray&, const QString& diagnostic) {
                if (code != 0) {
                    emit error(diagnostic);
                    // Only offer recovery for an actual worktree-overwrite refusal,
                    // never for authentication, invalid refs or existing worktrees.
                    if (diagnostic.contains("would be overwritten") ||
                        diagnostic.contains("local changes"))
                        emit checkoutFailed(repository_, ref, detached, diagnostic);
                }
                emit operationFinished();
                refresh();
            });
        return;
    }
    auto snapshot = std::make_shared<QString>();
    const auto backup =
        "refs/litecode/recovery/checkouts/" + QUuid::createUuid().toString(QUuid::Id128);
    const auto fail = [this](const QString& diagnostic) {
        emit error(diagnostic);
        emit operationFinished();
        refresh();
    };
    const auto perform = [this, arguments, recovery, snapshot, backup, fail] {
        run(repository_, arguments,
            [this, recovery, snapshot, backup, fail](int code, const QByteArray&,
                                                     const QString& diagnostic) {
                // If switching failed, restore to the original branch. Migration
                // reapplies on the destination. Never pop/drop the recovery copy.
                if (!snapshot->isEmpty() &&
                    (code != 0 || recovery == GitCheckoutRecovery::Migrate)) {
                    run(repository_, {"stash", "apply", "--index", *snapshot},
                        [this, code, diagnostic, backup, fail](int applied, const QByteArray&,
                                                               const QString& message) {
                            if (applied != 0) {
                                fail(tr("Changes remain recoverable at %1. Reapplication needs "
                                        "manual resolution.\n%2\n%3")
                                         .arg(backup, diagnostic, message));
                                return;
                            }
                            if (code != 0) {
                                fail(diagnostic);
                                return;
                            }
                            emit operationFinished();
                            refresh();
                        });
                } else if (code != 0) {
                    fail(diagnostic);
                } else {
                    emit operationFinished();
                    refresh();
                }
            });
    };
    QStringList status{"status", "--porcelain=v1", "-z", "--untracked-files=all"};
    if (recovery == GitCheckoutRecovery::BackupAll)
        status.append("--ignored");
    run(repository_, status,
        [this, recovery, snapshot, backup, perform, fail](int code, const QByteArray& bytes,
                                                          const QString& diagnostic) {
            if (code != 0) {
                fail(diagnostic);
                return;
            }
            if (bytes.isEmpty()) {
                perform();
                return;
            }
            run(repository_,
                {"stash", "push",
                 recovery == GitCheckoutRecovery::BackupAll ? "--all" : "--include-untracked",
                 "--message", "LiteCode checkout recovery " + backup.section('/', -1)},
                [this, snapshot, backup, perform, fail](int stashed, const QByteArray&,
                                                        const QString& message) {
                    if (stashed != 0) {
                        fail(message);
                        return;
                    }
                    run(repository_, {"log", "-1", "--format=%H%x00%s", "refs/stash"},
                        [this, snapshot, backup, perform, fail](int resolved, const QByteArray& oid,
                                                                const QString& detail) {
                            if (resolved != 0) {
                                fail(detail);
                                return;
                            }
                            const auto fields = oid.split('\0');
                            if (fields.size() != 2 ||
                                !fields[1].contains(backup.section('/', -1).toUtf8())) {
                                fail(tr("Git's stash changed concurrently. Checkout was stopped; "
                                        "your recovery remains in the stash list."));
                                return;
                            }
                            *snapshot = QString::fromUtf8(fields[0]).trimmed();
                            run(repository_, {"update-ref", backup, *snapshot},
                                [this, backup, perform, fail](int stored, const QByteArray&,
                                                              const QString& error) {
                                    if (stored != 0) {
                                        fail(tr("Recovery remains in Git's stash list. %1")
                                                 .arg(error));
                                        return;
                                    }
                                    emit recoveryCopyCreated(backup);
                                    perform();
                                });
                        });
                });
        });
}
void GitService::listRecoveryCopies() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_,
        {"for-each-ref", "--sort=-creatordate", "--format=%(refname)",
         "refs/litecode/recovery/checkouts/"},
        [this](int code, const QByteArray& bytes, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            QStringList refs;
            for (const auto& line : bytes.split('\n')) {
                const auto ref = QString::fromUtf8(line).trimmed();
                if (recoveryRef(ref))
                    refs.append(ref);
            }
            emit recoveryCopiesReady(refs);
        });
}
void GitService::restoreRecoveryCopy(const QString& ref) {
    if (!recoveryRef(ref)) {
        emit error(tr("Select a LiteCode checkout recovery copy."));
        return;
    }
    // Git refuses overlapping changes; no clean/reset/pop or automatic deletion.
    mutate({"stash", "apply", "--index", ref});
}
} // namespace litecode::workspace
