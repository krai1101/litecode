#include "workspace/GitService.h"
#include <QRegularExpression>

namespace litecode::workspace {
namespace {
bool validStashRef(const QString& ref) {
    static const QRegularExpression pattern(R"(^stash@\{[0-9]+\}$)");
    return pattern.match(ref).hasMatch();
}
} // namespace

void GitService::stash(bool includeUntracked, bool stagedOnly, const QString& message) {
    if (includeUntracked && stagedOnly || message.contains(QChar('\0'))) {
        emit error(tr("Invalid stash options or message."));
        return;
    }
    if (stagedOnly && !supportsStagedStash()) {
        emit error(tr("Stash Staged requires Git 2.35 or newer."));
        return;
    }
    QStringList arguments{"stash", "push"};
    if (includeUntracked)
        arguments.append("--include-untracked");
    if (stagedOnly)
        arguments.append("--staged");
    if (!message.isEmpty())
        arguments.append({"-m", message});
    mutate(arguments);
}
void GitService::listStashes() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_, {"stash", "list", "--format=%gd%x00%gs"},
        [this](int code, const QByteArray& output, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            QStringList refs, descriptions;
            for (const auto& row : output.split('\n')) {
                const auto separator = row.indexOf('\0');
                if (separator < 0)
                    continue;
                const auto ref = QString::fromUtf8(row.left(separator));
                if (!validStashRef(ref))
                    continue;
                refs.append(ref);
                descriptions.append(QString::fromUtf8(row.mid(separator + 1)).trimmed());
            }
            emit stashesReady(refs, descriptions);
        });
}
void GitService::applyStash(const QString& ref, bool pop) {
    if (!validStashRef(ref)) {
        emit error(tr("Select a valid stash."));
        return;
    }
    mutate({"stash", pop ? "pop" : "apply", ref});
}
void GitService::dropStash(const QString& ref) {
    if (!validStashRef(ref)) {
        emit error(tr("Select a valid stash."));
        return;
    }
    mutate({"stash", "drop", ref});
}
void GitService::dropAllStashes() { mutate({"stash", "clear"}); }
void GitService::viewStash(const QString& ref) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!validStashRef(ref)) {
        emit error(tr("Select a valid stash."));
        return;
    }
    run(repository_, {"stash", "show", "--include-untracked", "--patch", ref},
        [this, ref](int code, const QByteArray& output, const QString& diagnostic) {
            if (code != 0)
                emit error(diagnostic);
            else
                emit stashPatchReady(ref, QString::fromUtf8(output));
        });
}
} // namespace litecode::workspace
