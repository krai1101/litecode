#include "workspace/GitService.h"
#include <QDir>
#include <QFileInfo>

namespace litecode::workspace {
namespace {
QStringList parseWorktreePaths(const QByteArray& output) {
    QStringList paths;
    for (const auto& field : output.split('\0')) {
        if (field.startsWith("worktree "))
            paths.append(QDir::cleanPath(QString::fromUtf8(field.mid(9))));
    }
    return paths;
}
bool samePath(const QString& left, const QString& right) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), sensitivity) == 0;
}
} // namespace

void GitService::listWorktrees() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_, {"worktree", "list", "--porcelain", "-z"},
        [this](int code, const QByteArray& output, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Could not list worktrees.") : diagnostic);
                return;
            }
            emit worktreesReady(parseWorktreePaths(output));
        });
}

void GitService::deleteWorktree(const QString& path) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!QFileInfo(QDir(repository_).filePath(".git")).isDir()) {
        emit error(tr("Open the main repository before deleting a worktree."));
        return;
    }
    const QFileInfo target(path);
    if (!target.isAbsolute() || !target.isDir() || target.isSymLink() ||
        samePath(target.canonicalFilePath(), QDir(repository_).canonicalPath())) {
        emit error(tr("Select an existing linked worktree, not the main repository."));
        return;
    }
    const QString canonicalTarget = QDir::cleanPath(target.canonicalFilePath());
    run(repository_, {"worktree", "list", "--porcelain", "-z"},
        [this, canonicalTarget](int code, const QByteArray& output,
                                const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Could not verify the worktree.")
                                                : diagnostic);
                return;
            }
            const auto paths = parseWorktreePaths(output);
            bool registered = false;
            for (const auto& candidate : paths) {
                if (samePath(QFileInfo(candidate).canonicalFilePath(), canonicalTarget)) {
                    registered = true;
                    break;
                }
            }
            if (!registered) {
                emit error(tr("The selected folder is not a registered worktree."));
                return;
            }
            run(repository_, {"worktree", "remove", "--", canonicalTarget},
                [this, canonicalTarget](int removeCode, const QByteArray&,
                                        const QString& removeDiagnostic) {
                    if (removeCode != 0) {
                        emit error(removeDiagnostic.isEmpty()
                                       ? tr("Git refused to remove the worktree. Check for uncommitted changes.")
                                       : removeDiagnostic);
                        return;
                    }
                    emit operationFinished();
                    emit worktreeDeleted(canonicalTarget);
                    refresh();
                });
        });
}

void GitService::createWorktree(const QString& path, const QString& ref,
                                const QString& newBranch) {
    if (busy_ || repository_.isEmpty())
        return;
    const QFileInfo destination(QDir::cleanPath(path));
    const QString parent = destination.dir().canonicalPath();
    if (!destination.isAbsolute() || destination.fileName().isEmpty() || parent.isEmpty() ||
        destination.exists() || destination.isSymLink()) {
        emit error(tr("Choose a new worktree path whose parent folder exists."));
        return;
    }
    const QString target = QDir::fromNativeSeparators(destination.absoluteFilePath());
    const QString root = QDir::fromNativeSeparators(QDir(repository_).canonicalPath());
    if (target.compare(root, Qt::CaseInsensitive) == 0 ||
        target.startsWith(root + '/', Qt::CaseInsensitive)) {
        emit error(tr("Create worktrees outside the current repository."));
        return;
    }
    if (newBranch.isEmpty() &&
        !(ref.startsWith("refs/heads/") || ref.startsWith("refs/remotes/") ||
          ref.startsWith("refs/tags/"))) {
        emit error(tr("Select a branch or tag for the new worktree."));
        return;
    }
    const auto add = [this, target, ref, newBranch] {
        QStringList arguments{"worktree", "add"};
        if (!newBranch.isEmpty())
            arguments.append({"-b", newBranch});
        const QString commitish = !newBranch.isEmpty()
                                      ? QStringLiteral("HEAD")
                                      : ref.startsWith("refs/heads/") ? ref.mid(11) : ref;
        arguments.append({"--", target, commitish});
        run(repository_, arguments,
            [this, target](int code, const QByteArray&, const QString& diagnostic) {
                if (code != 0) {
                    emit error(diagnostic.isEmpty() ? tr("Creating the worktree failed.")
                                                    : diagnostic);
                    return;
                }
                emit operationFinished();
                emit worktreeCreated(target);
                refresh();
            });
    };
    if (newBranch.isEmpty()) {
        add();
        return;
    }
    if (newBranch.startsWith('-') || newBranch.contains(QChar('\0')) ||
        newBranch.contains(QChar('\n')) || newBranch.contains(QChar('\r'))) {
        emit error(tr("Enter a valid new branch name."));
        return;
    }
    run(repository_, {"check-ref-format", "--branch", newBranch},
        [this, add](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Invalid new branch name.") : diagnostic);
                return;
            }
            add();
        });
}
} // namespace litecode::workspace
