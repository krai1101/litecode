#include "workspace/GitService.h"
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace litecode::workspace {
namespace {
QString targetNameFor(const QString& source) {
    QString name = source.trimmed();
    while (name.endsWith('/') || name.endsWith('\\'))
        name.chop(1);
    name = name.mid(
        qMax(name.lastIndexOf('/'), qMax(name.lastIndexOf('\\'), name.lastIndexOf(':'))) + 1);
    if (name.endsWith(".git", Qt::CaseInsensitive))
        name.chop(4);
    return name;
}
} // namespace

void GitService::cloneRepository(const QString& url, const QString& parentDirectory) {
    if (busy_)
        return;
    const QString source = url.trimmed();
    const QString name = targetNameFor(source);
    if (source.isEmpty() || name.isEmpty() || name == "." || name == ".." ||
        name.contains(QChar('\0')) || name.contains(QChar('\n')) || name.contains(QChar('\r'))) {
        emit error(tr("Enter a valid repository URL or path to clone."));
        return;
    }
    const QString parent = QDir(parentDirectory).canonicalPath();
    if (parent.isEmpty()) {
        emit error(tr("Choose an existing destination folder for the clone."));
        return;
    }
    const QString destination = QDir(parent).filePath(name);
    const QFileInfo target(destination);
    if (target.exists() || target.isSymLink()) {
        emit error(tr("The clone destination already exists: %1").arg(destination));
        return;
    }
    cloneCancellationRequested_ = false;
    run(parent, {"clone", "--", source, name},
        [this, destination](int code, const QByteArray&, const QString& diagnostic) {
            if (cloneCancellationRequested_) {
                cloneCancellationRequested_ = false;
                emit cloneCancelled(destination);
                return;
            }
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Git clone failed. The destination may be "
                                                     "incomplete; inspect it before retrying.")
                                                : tr("Git clone failed: %1\nThe destination may be "
                                                     "incomplete; inspect it before retrying.")
                                                      .arg(diagnostic));
                return;
            }
            emit operationFinished();
            emit repositoryCloned(destination);
            refresh();
        });
}
void GitService::cancelClone() {
    if (!busy_ || currentOperation_ != "clone" || cloneCancellationRequested_)
        return;
    cloneCancellationRequested_ = true;
#ifdef Q_OS_WIN
    // Git for Windows may spawn git-remote-http or SSH helpers. Terminate the
    // process tree rooted at our own Git PID so cancel cannot leave helpers
    // holding a partial destination open. Never use a PID supplied externally.
    const qint64 gitPid = process_.processId();
    wchar_t systemDirectory[MAX_PATH]{};
    const UINT systemDirectoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (gitPid > 0 && systemDirectoryLength > 0 && systemDirectoryLength < MAX_PATH) {
        const QString taskkill =
            QDir(QString::fromWCharArray(systemDirectory)).filePath("taskkill.exe");
        auto* killer = new QProcess(this);
        const auto killOriginal = [this, gitPid] {
            if (cloneCancellationRequested_ && currentOperation_ == "clone" &&
                process_.processId() == gitPid)
                process_.kill();
        };
        connect(killer, &QProcess::finished, this,
                [killer, killOriginal](int code, QProcess::ExitStatus status) {
                    if (status != QProcess::NormalExit || code != 0)
                        killOriginal();
                    killer->deleteLater();
                });
        connect(killer, &QProcess::errorOccurred, this,
                [killer, killOriginal](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart) {
                        killOriginal();
                        killer->deleteLater();
                    }
                });
        QTimer::singleShot(5000, killer, [killer, killOriginal] {
            killOriginal();
            killer->kill();
        });
        killer->start(taskkill, {"/T", "/F", "/PID", QString::number(gitPid)});
        return;
    }
#endif
    process_.kill();
}
} // namespace litecode::workspace
