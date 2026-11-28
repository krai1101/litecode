#include "workspace/GitService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace litecode::workspace {
namespace {
bool safeRelativePath(const QString& path) {
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains(QChar('\0')) &&
           !path.contains(QChar('\\')) && QDir::cleanPath(path) == path &&
           path != QStringLiteral("..") && !path.startsWith(QStringLiteral("../"));
}
} // namespace

void GitService::discardWorkingChanges(bool includeUntracked) {
    if (busy_ || repository_.isEmpty())
        return;

    QStringList tracked, untracked;
    QSet<QString> seenTracked, seenUntracked;
    const auto append = [this](QStringList& paths, QSet<QString>& seen, const QString& path) {
        if (!safeRelativePath(path)) {
            emit error(tr("Git reported an invalid changed path; nothing was discarded."));
            return false;
        }
        if (!seen.contains(path)) {
            seen.insert(path);
            paths.append(path);
        }
        return true;
    };
    for (const auto& change : status_.changes) {
        if (change.conflict)
            continue;
        if (change.index == '?') {
            if (includeUntracked && !append(untracked, seenUntracked, change.path))
                return;
        } else if (change.worktree == 'R') {
            if (!append(tracked, seenTracked, change.originalPath))
                return;
            if (includeUntracked && !append(untracked, seenUntracked, change.path))
                return;
        } else if (change.worktree != ' ') {
            if (!append(tracked, seenTracked, change.path))
                return;
        }
    }
    if (tracked.isEmpty() && untracked.isEmpty())
        return;

    const QString root = repository_;
    const auto discardUntracked = [this, root, untracked] {
        QStringList failures;
        for (const auto& path : untracked) {
            const QString absolute = QDir(root).absoluteFilePath(path);
            const QFileInfo info(absolute);
            if ((!info.exists() && !info.isSymLink()) || !QFile::moveToTrash(absolute))
                failures.append(path);
        }
        if (!failures.isEmpty())
            emit error(tr("Could not move these untracked files to Trash: %1")
                           .arg(failures.join(QStringLiteral(", "))));
        emit operationFinished();
        refresh();
    };
    if (tracked.isEmpty()) {
        discardUntracked();
        return;
    }
    run(root, QStringList{"restore", "--worktree", "--"} + tracked,
        [this, discardUntracked](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Could not discard tracked changes.")
                                                : diagnostic);
                emit operationFinished();
                refresh();
                return;
            }
            discardUntracked();
        });
}
} // namespace litecode::workspace
