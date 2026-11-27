#include "workspace/GitService.h"
#include <QDir>
#include <QRegularExpression>
#include <memory>
namespace litecode::workspace {
static bool objectId(const QString& id) {
    static const QRegularExpression pattern("^(?:[0-9a-f]{40}|[0-9a-f]{64})$");
    return pattern.match(id).hasMatch();
}
QVector<GitCommit> GitService::parseHistory(const QByteArray& bytes) {
    const auto fields = bytes.split('\0');
    QVector<GitCommit> result;
    for (int index = 0; index + 5 < fields.size(); index += 6) {
        GitCommit commit;
        commit.id = QString::fromUtf8(fields[index]);
        if (!objectId(commit.id))
            continue;
        commit.parents = QString::fromUtf8(fields[index + 1]).split(' ', Qt::SkipEmptyParts);
        commit.author = QString::fromUtf8(fields[index + 2]);
        commit.timestamp = fields[index + 3].toLongLong();
        commit.subject = QString::fromUtf8(fields[index + 4]);
        commit.references = QString::fromUtf8(fields[index + 5]);
        result.append(commit);
    }
    return result;
}
void GitService::listHistory(int limit, const QStringList& references, bool automatic) {
    if (busy_ || repository_.isEmpty())
        return;
    for (const auto& ref : references)
        if (!ref.startsWith("refs/") || ref.contains(QChar('\0')) || ref.contains('\n') ||
            ref.contains('\r')) {
            emit error(tr("Select repository references to filter history."));
            return;
        }
    const bool unborn = status_.branch.startsWith("No commits yet on ") ||
                        status_.branch.startsWith("Initial commit on ");
    if (automatic && unborn) {
        emit historyReady({});
        return;
    }
    const auto load = [this, limit, unborn](const QStringList& refs, bool all) {
        QStringList arguments{"log",
                              "--topo-order",
                              "--decorate=full",
                              "--no-color",
                              "-z",
                              "--max-count=" + QString::number(qBound(1, limit, 2000)),
                              "--format=%H%x00%P%x00%an%x00%at%x00%s%x00%D"};
        if (all) {
            // Recovery/stash refs are deliberately excluded from normal history.
            arguments.append({"--branches", "--remotes", "--tags"});
            if (!unborn)
                arguments.append("HEAD");
        } else
            arguments.append(refs);
        arguments.append("--");
        run(repository_, arguments,
            [this](int code, const QByteArray& bytes, const QString& diagnostic) {
                if (code != 0)
                    emit error(diagnostic);
                else
                    emit historyReady(parseHistory(bytes));
            });
    };
    if (!automatic) {
        load(references, references.isEmpty());
        return;
    }
    run(repository_, {"for-each-ref", "--format=%(HEAD)%00%(upstream)", "refs/heads/"},
        [this, load](int code, const QByteArray& bytes, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            QStringList refs{"HEAD"};
            for (const auto& row : bytes.split('\n')) {
                const auto fields = row.split('\0');
                if (fields.size() == 2 && fields[0] == "*" && !fields[1].isEmpty())
                    refs.append(QString::fromUtf8(fields[1]));
            }
            load(refs, false);
        });
}
QVector<GitCommitFile> GitService::parseCommitFiles(const QByteArray& bytes) {
    const auto fields = bytes.split('\0');
    QVector<GitCommitFile> result;
    for (int index = 0; index + 1 < fields.size();) {
        const auto status = fields[index++];
        if (status.isEmpty())
            break;
        GitCommitFile file;
        file.status = status[0];
        file.path = QString::fromUtf8(fields[index++]);
        if (file.status == 'R' || file.status == 'C') {
            if (index >= fields.size())
                break;
            file.originalPath = file.path;
            file.path = QString::fromUtf8(fields[index++]);
        }
        result.append(file);
    }
    return result;
}
void GitService::listCommitFiles(const QString& commit, const QString& parent) {
    if (busy_ || repository_.isEmpty() || !objectId(commit) ||
        (!parent.isEmpty() && !objectId(parent)))
        return;
    QStringList arguments{"diff-tree", "--no-commit-id", "--name-status", "-z", "-r", "-M"};
    if (parent.isEmpty())
        arguments.append("--root");
    else
        arguments.append(parent);
    arguments.append(commit);
    arguments.append("--");
    run(repository_, arguments,
        [this, commit, parent](int code, const QByteArray& bytes, const QString& message) {
            if (code)
                emit error(message);
            else {
                const auto files = parseCommitFiles(bytes);
                emit commitFilesReady(commit, files);
                emit revisionFilesReady(commit, parent, files);
            }
        });
}
void GitService::compareCommitFile(const QString& commit, const QString& parent,
                                   const GitCommitFile& file) {
    // Revision paths are independent of current working-tree status.
    const auto safePath = [](const QString& path) {
        return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains(QChar('\0')) &&
               !path.contains('\\') && QDir::cleanPath(path) == path && path != ".." &&
               !path.startsWith("../");
    };
    if (busy_ || repository_.isEmpty() || !objectId(commit) ||
        (!parent.isEmpty() && !objectId(parent)) || !safePath(file.path) ||
        (!file.originalPath.isEmpty() && !safePath(file.originalPath)))
        return;
    auto comparison = std::make_shared<GitComparison>();
    comparison->path = file.path;
    comparison->originalPath = file.originalPath.isEmpty() ? file.path : file.originalPath;
    comparison->repository = repository_;
    comparison->historical = true;
    comparison->staged = true;
    comparison->originalRevision = parent;
    comparison->modifiedRevision = commit;
    comparison->modifiedExists = file.status != 'D';
    auto originalReady = [this, comparison, commit](int code, const QByteArray& bytes,
                                                    const QString& message) {
        if (code) {
            emit error(message);
            return;
        }
        comparison->original = bytes;
        auto modifiedReady = [this, comparison](int result, const QByteArray& contents,
                                                const QString& errorMessage) {
            if (result) {
                emit error(errorMessage);
                return;
            }
            comparison->modified = contents;
            comparison->binary = comparison->original.contains('\0') || contents.contains('\0');
            emit comparisonReady(*comparison);
        };
        if (!comparison->modifiedExists)
            modifiedReady(0, {}, {});
        else
            run(repository_, {"show", commit + ':' + comparison->path}, modifiedReady);
    };
    if (parent.isEmpty() || file.status == 'A')
        originalReady(0, {}, {});
    else
        run(repository_, {"show", parent + ':' + comparison->originalPath}, originalReady);
}
} // namespace litecode::workspace
