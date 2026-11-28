#include "workspace/GitService.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <memory>

namespace litecode::workspace {
namespace {
QString findGitExecutable() {
#ifdef Q_OS_WIN
    // Match VS Code's Windows discovery order before consulting PATH. Git for
    // Windows can be installed normally without adding git.exe to PATH.
    const auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto& key : {"ProgramW6432", "ProgramFiles(x86)", "ProgramFiles", "LocalAppData"}) {
        QString base = environment.value(QString::fromLatin1(key));
        if (base.isEmpty())
            continue;
        if (QString::fromLatin1(key) == QStringLiteral("LocalAppData"))
            base = QDir(base).filePath(QStringLiteral("Programs"));
        const QString candidate = QDir(base).filePath(QStringLiteral("Git/cmd/git.exe"));
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("git"));
}
} // namespace

GitService::GitService(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        process_.kill();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        output_ += process_.readAllStandardOutput();
        if (output_.size() > 32 * 1024 * 1024) {
            timedOut_ = true;
            process_.kill();
        }
    });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        diagnostic_ += process_.readAllStandardError();
        if (diagnostic_.size() > 1024 * 1024)
            diagnostic_.truncate(1024 * 1024);
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        output_ += process_.readAllStandardOutput();
        diagnostic_ += process_.readAllStandardError();
        if (output_.size() > 32 * 1024 * 1024) {
            output_.truncate(32 * 1024 * 1024);
            timedOut_ = true;
        }
        diagnostic_.truncate(qMin(diagnostic_.size(), qsizetype(1024 * 1024)));
        finish(exit == QProcess::NormalExit && !timedOut_ ? code : -1,
               timedOut_ ? tr("Git operation exceeded its time or output limit.")
                         : QString::fromUtf8(diagnostic_).trimmed());
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(-1, process_.errorString());
    });

    const QString executable = findGitExecutable();
    if (executable.isEmpty()) {
        stagedStashSupported_ = false;
        return;
    }
    auto* version = new QProcess(this);
    connect(
        version, &QProcess::finished, this, [this, version](int code, QProcess::ExitStatus status) {
            static const QRegularExpression pattern(QStringLiteral("git version (\\d+)\\.(\\d+)"));
            const auto match = pattern.match(QString::fromUtf8(version->readAllStandardOutput()));
            const int major = match.captured(1).toInt();
            const int minor = match.captured(2).toInt();
            stagedStashSupported_ = status == QProcess::NormalExit && code == 0 &&
                                    (major > 2 || (major == 2 && minor >= 35));
            version->deleteLater();
        });
    connect(version, &QProcess::errorOccurred, this, [this, version](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            stagedStashSupported_ = false;
            version->deleteLater();
        }
    });
    QTimer::singleShot(2000, version, &QProcess::kill);
    version->start(executable, {"--version"});
}
GitService::~GitService() {
    if (auto* app = QCoreApplication::instance(); app && !authenticationOperation_.isEmpty())
        app->setProperty(("litecode.git.active." + authenticationOperation_).toUtf8(), QVariant{});
    // Complete process teardown while this object's callbacks are still valid.
    process_.disconnect(this);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}
bool GitService::supportsStagedStash() const {
    // Git validates --staged itself if a request arrives before discovery finishes.
    return stagedStashSupported_.value_or(true);
}
void GitService::finish(int code, const QString& diagnostic) {
    if (!done_)
        return;
    timeout_.stop();
    if (auto* app = QCoreApplication::instance(); app && !authenticationOperation_.isEmpty())
        app->setProperty(("litecode.git.active." + authenticationOperation_).toUtf8(), QVariant{});
    authenticationOperation_.clear();
    auto done = std::move(done_);
    const auto output = std::move(output_);
    emit outputLine(tr("%1 Git: %2 exited with code %3")
                        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"), currentOperation_,
                             QString::number(code)));
    currentOperation_.clear();
    busy_ = false;
    emit busyChanged(false);
    done(code, output, diagnostic);
}
void GitService::run(const QString& directory, const QStringList& arguments, Done done) {
    if (busy_)
        return;
    const QString executable = findGitExecutable();
    gitInstalled_ = !executable.isEmpty();
    if (executable.isEmpty()) {
        emit outputLine(tr("Git executable not found."));
        done(-1, {},
             tr("Git installation not found. Install Git to use Source Control, then refresh or "
                "restart LiteCode."));
        return;
    }
    busy_ = true;
    // Do not log arguments, environment, stdout or stderr: remote URLs and
    // process output can contain credentials supplied by Git helpers.
    currentOperation_ = arguments.value(0) == "-c" ? arguments.value(2) : arguments.value(0);
    emit outputLine(tr("%1 Git: %2 started")
                        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"), currentOperation_));
    emit busyChanged(true);
    output_.clear();
    diagnostic_.clear();
    timedOut_ = false;
    const auto epoch = epoch_;
    done_ = [this, epoch, done = std::move(done)](int code, const QByteArray& output,
                                                  const QString& diagnostic) {
        if (epoch == epoch_)
            done(code, output, diagnostic);
        else
            refresh();
    };
    auto environment = QProcessEnvironment::systemEnvironment();
    // Do not inherit another application's repository/index selection.
    for (const auto& key :
         {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR", "GIT_LITERAL_PATHSPECS",
          "GIT_GLOB_PATHSPECS", "GIT_NOGLOB_PATHSPECS", "GIT_ICASE_PATHSPECS"})
        environment.remove(QString::fromLatin1(key));
    environment.insert("GIT_TERMINAL_PROMPT", "0");
    environment.insert("LC_ALL", "C");
    environment.insert("GIT_OPTIONAL_LOCKS", "0");
    if (auto* app = QCoreApplication::instance()) {
        auto helper = QCoreApplication::applicationDirPath() + "/LiteCodeGitAskPass";
#ifdef Q_OS_WIN
        helper += ".exe";
#endif
        const auto endpoint = app->property("litecode.git.askpass.endpoint").toString();
        const auto token = app->property("litecode.git.askpass.token").toString();
        if (!endpoint.isEmpty() && !token.isEmpty() && QFileInfo::exists(helper)) {
            authenticationOperation_ = QUuid::createUuid().toString(QUuid::Id128);
            app->setProperty(("litecode.git.active." + authenticationOperation_).toUtf8(), true);
            environment.insert("GIT_ASKPASS", QDir::toNativeSeparators(helper));
            environment.insert("SSH_ASKPASS", QDir::toNativeSeparators(helper));
            environment.insert("SSH_ASKPASS_REQUIRE", "force");
            environment.insert("LITECODE_GIT_ASKPASS_ENDPOINT", endpoint);
            environment.insert("LITECODE_GIT_ASKPASS_TOKEN", token);
            environment.insert("LITECODE_GIT_ASKPASS_OPERATION", authenticationOperation_);
        }
    }
    process_.setProcessEnvironment(environment);
    process_.setWorkingDirectory(directory);
    process_.setProgram(executable);
    QStringList globalArguments{"--no-pager"};
    // Stash invokes internal pathspec-aware Git commands. A global literal
    // flag leaks into those commands and breaks their :/ cleanup pathspec.
    // Only operations accepting user file paths need literal path matching.
    if (QStringList{"add", "reset", "diff", "restore", "ls-files"}.contains(arguments.value(0)))
        globalArguments.append("--literal-pathspecs");
    process_.setArguments(globalArguments + arguments);
    timeout_.start(QStringList{"clone", "fetch", "pull", "push"}.contains(arguments.value(0)) ||
                           arguments.contains("pull")
                       ? 300000
                       : 60000);
    process_.start();
    process_.closeWriteChannel();
}
void GitService::setWorkspace(const QString& path) {
    ++epoch_;
    workspace_ = path;
    repository_.clear();
    status_ = {};
    gitInstalled_ = false;
    emit changed();
    // Do not terminate a write midway when switching workspaces.
    if (!busy_)
        refresh();
}
void GitService::initializeRepository() {
    if (busy_ || !repository_.isEmpty() || workspace_.isEmpty() || !QFileInfo(workspace_).isDir())
        return;
    run(workspace_, {"init"}, [this](int code, const QByteArray&, const QString& diagnostic) {
        if (code != 0)
            emit error(diagnostic);
        else
            emit operationFinished();
        refresh();
    });
}
void GitService::refresh() {
    if (busy_ || workspace_.isEmpty())
        return;
    run(workspace_, {"rev-parse", "--show-toplevel"},
        [this](int code, const QByteArray& output, const QString& diagnostic) {
            if (code != 0) {
                repository_.clear();
                status_ = {};
                emit changed();
                if (code == -1)
                    emit error(diagnostic);
                return;
            }
            repository_ = QString::fromUtf8(output).trimmed();
            run(repository_,
                {"status", "--porcelain=v1", "-z", "--branch", "--untracked-files=all"},
                [this](int result, const QByteArray& bytes, const QString& message) {
                    if (result != 0) {
                        emit error(message);
                        return;
                    }
                    status_ = parseStatus(bytes);
                    emit changed();
                });
        });
}
GitStatus GitService::parseStatus(const QByteArray& bytes) {
    GitStatus result;
    const auto records = bytes.split('\0');
    for (qsizetype i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (record.startsWith("## ")) {
            result.branch = QString::fromUtf8(record.mid(3));
            continue;
        }
        if (record.size() < 4)
            continue;
        GitChange change;
        change.index = record[0];
        change.worktree = record[1];
        change.path = QString::fromUtf8(record.mid(3));
        if (change.index == 'R' || change.index == 'C' || change.worktree == 'R' ||
            change.worktree == 'C') {
            if (i + 1 < records.size())
                change.originalPath = QString::fromUtf8(records[++i]);
        }
        const auto xy = record.left(2);
        change.conflict = xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" || xy == "DU" ||
                          xy == "AA" || xy == "UU";
        result.changes.append(change);
    }
    return result;
}
bool GitService::validPaths(const QStringList& paths) const {
    if (repository_.isEmpty() || paths.isEmpty())
        return false;
    for (const auto& path : paths) {
        if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QChar('\0')) ||
            path.contains('\\') || QDir::cleanPath(path) != path || path == ".." ||
            path.startsWith("../"))
            return false;
        bool known = false;
        for (const auto& change : status_.changes)
            if (path == change.path || path == change.originalPath) {
                known = true;
                break;
            }
        if (!known)
            return false;
    }
    return true;
}
void GitService::mutate(const QStringList& arguments) {
    if (busy_) {
        emit error(tr("Git is busy. Wait for the current operation to finish."));
        return;
    }
    if (repository_.isEmpty()) {
        emit error(tr("Open a Git repository first."));
        return;
    }
    const bool committing = arguments.value(0) == QStringLiteral("commit");
    const bool pulling = arguments.contains(QStringLiteral("pull"));
    run(repository_, arguments,
        [this, committing, pulling](int code, const QByteArray& output, const QString& diagnostic) {
            if (code != 0) {
                emit error(diagnostic.isEmpty() ? tr("Git operation failed.") : diagnostic);
                if (diagnostic.contains("would clobber existing tag"))
                    emit tagConflict(repository_, diagnostic);
            } else if (pulling &&
                       (QString::fromUtf8(output) + diagnostic)
                           .contains("autostash resulted in conflicts", Qt::CaseInsensitive)) {
                emit error(tr("Pull completed, but restoring your local changes caused conflicts. "
                              "Git retained the auto-stash; resolve the conflicts before another "
                              "pull.\n%1")
                               .arg(diagnostic));
            } else if (committing)
                emit commitSucceeded();
            emit operationFinished();
            refresh();
        });
}
void GitService::stage(const QStringList& paths) {
    if (!validPaths(paths)) {
        emit error(tr("Select repository-relative changed files."));
        return;
    }
    mutate(QStringList{"add", "--"} + paths);
}
void GitService::unstage(const QStringList& paths) {
    if (!validPaths(paths)) {
        emit error(tr("Select repository-relative staged files."));
        return;
    }
    // update-index removes newly added entries; reset also works before the first commit.
    mutate(QStringList{"reset", "--"} + paths);
}
void GitService::commit(const QString& message) {
    if (message.trimmed().isEmpty()) {
        emit error(tr("Enter a commit message."));
        return;
    }
    bool staged = false;
    for (const auto& change : status_.changes) {
        if (change.conflict) {
            emit error(tr("Resolve and stage merge conflicts before committing."));
            return;
        }
        staged |= change.index != ' ' && change.index != '?';
    }
    if (!staged) {
        emit error(tr("Stage changes before committing."));
        return;
    }
    mutate({"commit", "-m", message});
}
void GitService::diff(const QString& path, bool staged) {
    if (busy_ || !validPaths({path}))
        return;
    QStringList arguments{"diff", "--no-ext-diff", "--no-textconv", "--no-color"};
    if (staged)
        arguments.append("--cached");
    arguments.append({"--", path});
    run(repository_, arguments,
        [this, path, staged](int code, const QByteArray& output, const QString& message) {
            if (code != 0)
                emit error(message);
            else
                emit diffReady(path, QString::fromUtf8(output), staged);
        });
}
void GitService::compare(const QString& path, bool staged) {
    if (busy_ || !validPaths({path}))
        return;
    GitChange change;
    for (const auto& entry : status_.changes)
        if (entry.path == path)
            change = entry;
    if (change.conflict) {
        emit error(tr("Resolve this merge conflict in the file editor."));
        return;
    }
    auto comparison = std::make_shared<GitComparison>();
    comparison->path = path;
    comparison->repository = repository_;
    comparison->staged = staged;
    comparison->originalPath =
        staged && !change.originalPath.isEmpty() ? change.originalPath : path;
    const bool originalMissing = staged ? change.index == 'A' : change.index == '?';
    comparison->modifiedExists = staged ? change.index != 'D' : change.worktree != 'D';
    auto readModified = [this, comparison](int code, const QByteArray& bytes,
                                           const QString& message) {
        if (code != 0) {
            emit error(message);
            return;
        }
        comparison->original = bytes;
        auto ready = [this, comparison](int result, const QByteArray& contents,
                                        const QString& diagnostic) {
            if (result != 0) {
                emit error(diagnostic);
                return;
            }
            comparison->modified = contents;
            comparison->binary = comparison->original.contains('\0') || contents.contains('\0');
            emit comparisonReady(*comparison);
        };
        if (!comparison->modifiedExists) {
            ready(0, {}, {});
        } else if (comparison->staged) {
            run(repository_, {"show", ":" + comparison->path}, ready);
        } else {
            const QString target = QDir(repository_).filePath(comparison->path);
            const QFileInfo info(target);
            const QString canonicalRoot = QFileInfo(repository_).canonicalFilePath() + '/';
            if (info.isSymLink() ||
                !info.canonicalFilePath().startsWith(canonicalRoot, Qt::CaseInsensitive)) {
                ready(-1, {},
                      tr("Cannot compare a file outside this repository or a symbolic link."));
                return;
            }
            QFile file(target);
            if (file.size() > 32 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
                ready(-1, {},
                      tr("Cannot read this file, or it exceeds the 32 MiB comparison limit."));
                return;
            }
            const auto content = file.read(32 * 1024 * 1024 + 1);
            ready(content.size() <= 32 * 1024 * 1024 && file.error() == QFile::NoError ? 0 : -1,
                  content, tr("Could not read the complete file."));
        }
    };
    if (originalMissing)
        readModified(0, {}, {});
    else
        run(repository_, {"show", (staged ? "HEAD:" : ":") + comparison->originalPath},
            readModified);
}
namespace {
bool validArgument(const QString& argument) {
    return !argument.isEmpty() && !argument.startsWith('-') && !argument.contains(QChar('\0')) &&
           !argument.contains('\n') && !argument.contains('\r');
}
bool validRemoteName(const QString& name) {
    static const QRegularExpression pattern("^[a-zA-Z0-9_][a-zA-Z0-9_.-]*$");
    return pattern.match(name).hasMatch();
}
} // namespace
QVector<GitBranch> GitService::parseBranches(const QByteArray& bytes) {
    QVector<GitBranch> result;
    for (const auto& row : bytes.split('\n')) {
        const auto fields = row.split('\0');
        if (fields.size() < 5 || !fields[4].isEmpty())
            continue; // Skip remote HEAD aliases.
        const auto ref = QString::fromUtf8(fields[0]);
        const bool remote = ref.startsWith("refs/remotes/");
        const bool tag = ref.startsWith("refs/tags/");
        if (!remote && !tag && !ref.startsWith("refs/heads/"))
            continue;
        result.append({ref.mid(remote ? 13
                               : tag  ? 10
                                      : 11),
                       ref, QString::fromUtf8(fields[1]), QString::fromUtf8(fields[3]),
                       fields[2] == "*", remote, tag});
    }
    return result;
}
void GitService::listBranches() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_,
        {"for-each-ref", "--sort=-committerdate",
         "--format=%(refname)%00%(objectname)%00%(HEAD)%00%(upstream:short)%00%(symref)",
         "refs/heads/", "refs/remotes/", "refs/tags/"},
        [this](int code, const QByteArray& bytes, const QString& message) {
            if (code != 0)
                emit error(message);
            else
                emit branchesReady(parseBranches(bytes));
        });
}
void GitService::createBranch(const QString& name, const QString& startPoint) {
    if (busy_ || repository_.isEmpty())
        return;
    if (!validArgument(name) || name.contains("@{")) {
        emit error(tr("Enter a valid branch name, not a Git option."));
        return;
    }
    if (!startPoint.isEmpty() && !validArgument(startPoint)) {
        emit error(tr("Select a valid starting revision."));
        return;
    }
    run(repository_, {"check-ref-format", "--branch", name},
        [this, name, startPoint](int code, const QByteArray&, const QString& message) {
            if (code != 0)
                emit error(message.isEmpty() ? tr("Invalid branch name.") : message);
            else if (startPoint.isEmpty())
                mutate({"checkout", "-b", name, "--no-track"});
            else
                run(repository_,
                    {"rev-parse", "--verify", "--end-of-options", startPoint + "^{commit}"},
                    [this, name](int result, const QByteArray& bytes, const QString& diagnostic) {
                        if (result != 0)
                            emit error(diagnostic);
                        else
                            mutate({"checkout", "--no-track", "-b", name,
                                    QString::fromUtf8(bytes).trimmed()});
                    });
        });
}
void GitService::checkoutBranch(const QString& ref, GitCheckoutRecovery recovery) {
    if (busy_ || repository_.isEmpty())
        return;
    if (ref.startsWith("refs/tags/")) {
        checkoutDetached(ref, recovery);
        return;
    }
    if (!validArgument(ref) ||
        (!ref.startsWith("refs/heads/") && !ref.startsWith("refs/remotes/"))) {
        emit error(tr("Select a local or remote branch."));
        return;
    }
    // Never force checkout or auto-discard/stash user changes. Git reports unsafe switches.
    run(repository_, {"check-ref-format", ref},
        [this, ref, recovery](int code, const QByteArray&, const QString& message) {
            if (code != 0) {
                emit error(message.isEmpty() ? tr("Invalid branch reference.") : message);
                return;
            }
            if (ref.startsWith("refs/remotes/")) {
                // VS Code reuses an existing tracking branch rather than trying to
                // create the same local branch on every remote-branch selection.
                run(repository_,
                    {"for-each-ref", "--format=%(refname)%00%(upstream)", "refs/heads/"},
                    [this, ref, recovery](int result, const QByteArray& bytes,
                                          const QString& diagnostic) {
                        if (result != 0) {
                            emit error(diagnostic);
                            return;
                        }
                        for (const auto& row : bytes.split('\n')) {
                            const auto fields = row.split('\0');
                            if (fields.size() == 2 && QString::fromUtf8(fields[1]) == ref) {
                                checkoutCommand({"switch", "--no-guess", "--",
                                                 QString::fromUtf8(fields[0]).mid(11)},
                                                ref, false, recovery);
                                return;
                            }
                        }
                        checkoutCommand({"checkout", "--track", ref}, ref, false, recovery);
                    });
            } else
                checkoutCommand({"switch", "--no-guess", "--", ref.mid(11)}, ref, false, recovery);
        });
}
QVector<GitRemote> GitService::parseRemotes(const QByteArray& bytes) {
    QVector<GitRemote> result;
    for (const auto& row : bytes.split('\n')) {
        const auto tab = row.indexOf('\t');
        const auto suffix = row.lastIndexOf(" (");
        if (tab < 1 || suffix < tab)
            continue;
        const auto name = QString::fromUtf8(row.left(tab));
        const auto url = QString::fromUtf8(row.mid(tab + 1, suffix - tab - 1));
        auto it = std::find_if(result.begin(), result.end(),
                               [&](const auto& remote) { return remote.name == name; });
        if (it == result.end()) {
            result.append({name, {}, {}});
            it = result.end() - 1;
        }
        if (row.endsWith("(fetch)"))
            it->fetchUrl = url;
        if (row.endsWith("(push)"))
            it->pushUrl = url;
    }
    return result;
}
void GitService::listRemotes() {
    if (busy_ || repository_.isEmpty())
        return;
    run(repository_, {"remote", "-v"},
        [this](int code, const QByteArray& bytes, const QString& message) {
            if (code != 0)
                emit error(message);
            else
                emit remotesReady(parseRemotes(bytes));
        });
}
void GitService::addRemote(const QString& name, const QString& url) {
    if (!validRemoteName(name) || !validArgument(url)) {
        emit error(tr("Enter a valid remote name and URL or local repository path."));
        return;
    }
    mutate({"remote", "add", name, url});
}
void GitService::removeRemote(const QString& name) {
    if (!validRemoteName(name)) {
        emit error(tr("Select a valid remote."));
        return;
    }
    mutate({"remote", "remove", name});
}
void GitService::fetch(const QString& remote, bool all, bool prune) {
    if (!remote.isEmpty() && !validRemoteName(remote)) {
        emit error(tr("Select a valid remote."));
        return;
    }
    if (all && !remote.isEmpty()) {
        emit error(tr("Fetch All does not select a single remote."));
        return;
    }
    QStringList arguments{"fetch"};
    if (all)
        arguments.append("--all");
    if (prune || preferences_.pruneOnFetch)
        arguments.append("--prune");
    if (!remote.isEmpty())
        arguments.append(remote);
    mutate(arguments);
}
} // namespace litecode::workspace
