#include "workspace/GitService.h"
#include <QRegularExpression>
#include <QUuid>
namespace litecode::workspace {
namespace {
const QString tagBackupPrefix = "refs/litecode/recovery/tags/";
QString originalTag(const QString& backup) {
    static const QRegularExpression pattern("^refs/litecode/recovery/tags/[a-f0-9]{32}/(.+)$");
    const auto match = pattern.match(backup);
    return match.hasMatch() ? "refs/tags/" + match.captured(1) : QString();
}
} // namespace
void GitService::replaceRemoteTag(const QString& remote, const QString& tagRef) {
    if (busy_ || repository_.isEmpty())
        return;
    static const QRegularExpression remotePattern("^[a-zA-Z0-9_][a-zA-Z0-9_.-]*$");
    if (!remotePattern.match(remote).hasMatch() || !tagRef.startsWith("refs/tags/")) {
        emit error(tr("Select a configured remote and a local tag."));
        return;
    }
    run(repository_, {"check-ref-format", tagRef},
        [this, remote, tagRef](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            run(repository_, {"rev-parse", "--verify", tagRef},
                [this, remote, tagRef](int result, const QByteArray& oid, const QString& message) {
                    if (result != 0) {
                        emit error(message);
                        return;
                    }
                    const auto backup = tagBackupPrefix +
                                        QUuid::createUuid().toString(QUuid::Id128) + "/" +
                                        tagRef.mid(10);
                    const auto expected = QString::fromUtf8(oid).trimmed();
                    // Keep the exact tag object (including annotated tags) reachable.
                    run(repository_, {"update-ref", backup, QString::fromUtf8(oid).trimmed()},
                        [this, remote, tagRef, backup, expected](int stored, const QByteArray&,
                                                                 const QString& detail) {
                            if (stored != 0) {
                                emit error(detail);
                                return;
                            }
                            emit tagBackupCreated(backup);
                            // Fetch to a private ref, then compare-and-swap the local
                            // tag. Concurrent external edits must not be overwritten.
                            const auto fetched = "refs/litecode/fetched/tags/" +
                                                 QUuid::createUuid().toString(QUuid::Id128);
                            run(repository_, {"fetch", "--no-tags", remote, tagRef + ":" + fetched},
                                [this, tagRef, expected, fetched](int fetchedCode,
                                                                  const QByteArray&,
                                                                  const QString& fetchDiagnostic) {
                                    if (fetchedCode != 0) {
                                        emit error(fetchDiagnostic);
                                        emit operationFinished();
                                        refresh();
                                        return;
                                    }
                                    run(repository_, {"rev-parse", "--verify", fetched},
                                        [this, tagRef, expected](int resolved,
                                                                 const QByteArray& replacement,
                                                                 const QString& diagnostic) {
                                            if (resolved != 0) {
                                                emit error(diagnostic);
                                                emit operationFinished();
                                                refresh();
                                                return;
                                            }
                                            mutate({"update-ref", tagRef,
                                                    QString::fromUtf8(replacement).trimmed(),
                                                    expected});
                                        });
                                });
                        });
                });
        });
}
void GitService::listTagBackups() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_, {"for-each-ref", "--format=%(refname)", tagBackupPrefix},
        [this](int code, const QByteArray& bytes, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            QStringList result;
            for (const auto& line : bytes.split('\n')) {
                const auto ref = QString::fromUtf8(line);
                if (!originalTag(ref).isEmpty())
                    result.append(ref);
            }
            emit tagBackupsReady(result);
        });
}
void GitService::restoreTagBackup(const QString& backupRef) {
    if (busy_ || repository_.isEmpty())
        return;
    const auto tagRef = originalTag(backupRef);
    if (tagRef.isEmpty() || backupRef.contains(QChar('\0')) || backupRef.contains('\n')) {
        emit error(tr("Select a retained tag backup."));
        return;
    }
    run(repository_, {"check-ref-format", tagRef},
        [this, backupRef, tagRef](int code, const QByteArray&, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic);
                return;
            }
            run(repository_, {"rev-parse", "--verify", backupRef},
                [this, tagRef](int result, const QByteArray& oid, const QString& message) {
                    if (result != 0) {
                        emit error(message);
                        return;
                    }
                    const auto replacement = QString::fromUtf8(oid).trimmed();
                    run(repository_, {"show-ref", "--verify", "--quiet", tagRef},
                        [this, tagRef, replacement](int exists, const QByteArray&,
                                                    const QString& diagnostic) {
                            if (exists == 1) {
                                mutate({"update-ref", tagRef, replacement,
                                        QString(replacement.size(), '0')});
                                return;
                            }
                            if (exists != 0) {
                                emit error(diagnostic);
                                return;
                            }
                            run(repository_, {"rev-parse", "--verify", tagRef},
                                [this, tagRef, replacement](int resolved, const QByteArray& current,
                                                            const QString& detail) {
                                    if (resolved != 0) {
                                        emit error(detail);
                                        return;
                                    }
                                    const auto expected = QString::fromUtf8(current).trimmed();
                                    const auto backup = tagBackupPrefix +
                                                        QUuid::createUuid().toString(QUuid::Id128) +
                                                        "/" + tagRef.mid(10);
                                    run(repository_, {"update-ref", backup, expected},
                                        [this, tagRef, replacement, expected, backup](
                                            int stored, const QByteArray&, const QString& message) {
                                            if (stored != 0) {
                                                emit error(message);
                                                return;
                                            }
                                            emit tagBackupCreated(backup);
                                            mutate({"update-ref", tagRef, replacement, expected});
                                        });
                                });
                        });
                });
        });
}
} // namespace litecode::workspace
