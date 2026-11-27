#include "workspace/GitRepositories.h"
#include "workspace/GitService.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <algorithm>
using namespace litecode::workspace;
namespace {
QByteArray gitBytes(const QString& directory, const QStringList& arguments) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start("git", arguments);
    if (!process.waitForFinished(10000) || process.exitCode() != 0)
        return {};
    return process.readAllStandardOutput();
}
bool git(const QString& directory, const QStringList& arguments) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start("git", arguments);
    return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}
bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool initialize(const QString& directory) {
    return git(directory, {"init", "--initial-branch=main"}) &&
           git(directory, {"config", "user.name", "LiteCode Test"}) &&
           git(directory, {"config", "user.email", "test@example.invalid"}) &&
           git(directory, {"config", "core.autocrlf", "false"}) &&
           git(directory, {"config", "commit.gpgsign", "false"});
}
} // namespace
class GitServiceTests : public QObject {
    Q_OBJECT
  private slots:
    void cancelCloneDoesNotReportSuccessOrDeletePartialDestination() {
        QTemporaryDir destinationParent;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        GitService service;
        QSignalSpy cancelled(&service, &GitService::cloneCancelled);
        QSignalSpy cloned(&service, &GitService::repositoryCloned);
        QSignalSpy errors(&service, &GitService::error);
        const QString url = QString("http://127.0.0.1:%1/repo.git").arg(server.serverPort());
        service.cloneRepository(url, destinationParent.path());
        QVERIFY(service.busy());
        QTimer::singleShot(200, &service, &GitService::cancelClone);
        QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 10000);
        QCOMPARE(cancelled.first().first().toString(), destinationParent.filePath("repo"));
        QCOMPARE(cloned.size(), 0);
        QCOMPARE(errors.size(), 0);
        QVERIFY(!service.busy());
    }

    void createsWorktreeWithoutChangingCurrentBranchOrOverwritingTarget() {
        QTemporaryDir repository, destinationParent;
        QVERIFY(initialize(repository.path()));
        QVERIFY(writeFile(repository.filePath("base.txt"), "base\n"));
        QVERIFY(git(repository.path(), {"add", "."}));
        QVERIFY(git(repository.path(), {"commit", "-m", "base"}));
        const QString destination = destinationParent.filePath("feature");
        GitService service;
        QSignalSpy created(&service, &GitService::worktreeCreated);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(repository.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.createWorktree(destination, "HEAD", "feature");
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 15000);
        QCOMPARE(created.first().first().toString(), destination);
        QCOMPARE(gitBytes(destination, {"branch", "--show-current"}).trimmed(),
                 QByteArray("feature"));
        QCOMPARE(gitBytes(repository.path(), {"branch", "--show-current"}).trimmed(),
                 QByteArray("main"));
        QTRY_VERIFY(!service.busy());
        QVERIFY(git(repository.path(), {"branch", "spare"}));
        const QString spareDestination = destinationParent.filePath("spare");
        service.createWorktree(spareDestination, "refs/heads/spare");
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 2, 15000);
        QCOMPARE(gitBytes(spareDestination, {"branch", "--show-current"}).trimmed(),
                 QByteArray("spare"));
        QTRY_VERIFY(!service.busy());
        service.createWorktree(destination, "HEAD", "another");
        QCOMPARE(created.size(), 2);
        QCOMPARE(errors.size(), 1);
        service.createWorktree(repository.filePath("nested"), "HEAD", "another");
        QCOMPARE(errors.size(), 2);
    }

    void listsRegisteredWorktreesAndRefusesDirtyRemoval() {
        QTemporaryDir repository, destinationParent;
        QVERIFY(initialize(repository.path()));
        QVERIFY(writeFile(repository.filePath("base.txt"), "base\n"));
        QVERIFY(git(repository.path(), {"add", "."}));
        QVERIFY(git(repository.path(), {"commit", "-m", "base"}));
        const QString destination = destinationParent.filePath("linked");
        QVERIFY(git(repository.path(), {"worktree", "add", "-b", "linked", destination}));
        GitService service;
        QSignalSpy listed(&service, &GitService::worktreesReady);
        QSignalSpy deleted(&service, &GitService::worktreeDeleted);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(repository.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.listWorktrees();
        QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 10000);
        const auto paths = listed.first().first().toStringList();
        QVERIFY(paths.contains(QDir::cleanPath(destination)));
        service.deleteWorktree(repository.path());
        QCOMPARE(errors.size(), 1);
        QVERIFY(writeFile(destinationParent.filePath("linked/dirty.txt"), "dirty\n"));
        service.deleteWorktree(destination);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 10000);
        QVERIFY(QFileInfo(destination).isDir());
        QCOMPARE(deleted.size(), 0);
        QVERIFY(QFile::remove(destinationParent.filePath("linked/dirty.txt")));
        service.deleteWorktree(destination);
        QTRY_COMPARE_WITH_TIMEOUT(deleted.size(), 1, 10000);
        QVERIFY(!QFileInfo(destination).exists());
        QVERIFY(QFileInfo(repository.filePath("base.txt")).exists());
    }

    void cloneWorksWithoutOpenRepositoryAndNeverOverwritesDestination() {
        QTemporaryDir source, parent;
        QVERIFY(initialize(source.path()));
        QVERIFY(writeFile(source.filePath("readme.txt"), "cloned\n"));
        QVERIFY(git(source.path(), {"add", "."}));
        QVERIFY(git(source.path(), {"commit", "-m", "first"}));
        GitService service;
        QSignalSpy cloned(&service, &GitService::repositoryCloned);
        QSignalSpy errors(&service, &GitService::error);
        QSignalSpy output(&service, &GitService::outputLine);
        QVERIFY(service.repository().isEmpty());
        service.cloneRepository(source.path(), parent.path());
        QTRY_COMPARE_WITH_TIMEOUT(cloned.size(), 1, 15000);
        const QString destination = cloned.first().first().toString();
        QCOMPARE(destination, QDir(parent.path()).filePath(QFileInfo(source.path()).fileName()));
        QCOMPARE(QFile(destination + "/readme.txt").exists(), true);
        QCOMPARE(gitBytes(destination, {"rev-parse", "HEAD"}).trimmed(),
                 gitBytes(source.path(), {"rev-parse", "HEAD"}).trimmed());
        QVERIFY(output.size() >= 2);
        for (const auto& event : output)
            QVERIFY(!event.first().toString().contains(source.path()));
        service.cloneRepository(source.path(), parent.path());
        QCOMPARE(errors.size(), 1);
        QCOMPARE(cloned.size(), 1);
        service.cloneRepository("..", parent.path());
        QCOMPARE(errors.size(), 2);
        QCOMPARE(cloned.size(), 1);
    }

    void forcePushIsGatedAndLeaseProtectsUnseenRemoteWork() {
        QTemporaryDir local, remote, peer;
        QVERIFY(initialize(local.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(local.filePath("file.txt"), "base\n"));
        QVERIFY(git(local.path(), {"add", "."}));
        QVERIFY(git(local.path(), {"commit", "-m", "base"}));
        QVERIFY(git(local.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(local.path(), {"push", "--set-upstream", "origin", "main"}));
        QVERIFY(git(peer.path(), {"clone", remote.path(), "."}));
        QVERIFY(initialize(peer.path()));
        QVERIFY(writeFile(peer.filePath("file.txt"), "peer\n"));
        QVERIFY(git(peer.path(), {"add", "."}));
        QVERIFY(git(peer.path(), {"commit", "-m", "peer"}));
        QVERIFY(git(peer.path(), {"push", "origin", "main"}));
        const auto peerHead = gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed();
        QVERIFY(writeFile(local.filePath("file.txt"), "local\n"));
        QVERIFY(git(local.path(), {"add", "."}));
        QVERIFY(git(local.path(), {"commit", "-m", "local"}));
        const auto localHead = gitBytes(local.path(), {"rev-parse", "HEAD"}).trimmed();

        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(local.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.pushForce();
        service.pushToForce("origin");
        QCOMPARE(errors.size(), 2);
        QCOMPARE(completed.size(), 0);
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(), peerHead);

        litecode::core::GitPreferences preferences;
        preferences.allowForcePush = true;
        service.setPreferences(preferences);
        service.pushForce();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(), peerHead);
        QVERIFY(errors.size() >= 3); // --force-with-lease rejected the stale tracking ref.

        service.fetch("origin");
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 15000);
        QTRY_VERIFY(!service.busy());
        service.pushForce();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 3, 15000);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(), peerHead);
        preferences.useForcePushIfIncludes = false;
        service.setPreferences(preferences);
        service.pushForce();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 4, 15000);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(), localHead);
    }

    void explicitPullFromAndPushToUseSelectedRemoteAndBranch() {
        QTemporaryDir local, remote;
        QVERIFY(initialize(local.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(local.filePath("base.txt"), "base\n"));
        QVERIFY(git(local.path(), {"add", "."}));
        QVERIFY(git(local.path(), {"commit", "-m", "base"}));
        QVERIFY(git(local.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(local.path(), {"push", "--set-upstream", "origin", "main"}));
        QVERIFY(git(local.path(), {"checkout", "-b", "feature"}));
        QVERIFY(writeFile(local.filePath("feature.txt"), "feature\n"));
        QVERIFY(git(local.path(), {"add", "."}));
        QVERIFY(git(local.path(), {"commit", "-m", "feature"}));
        QVERIFY(git(local.path(), {"push", "origin", "feature"}));
        QVERIFY(git(local.path(), {"checkout", "main"}));
        QVERIFY(writeFile(local.filePath("local.txt"), "local\n"));
        QVERIFY(git(local.path(), {"add", "."}));
        QVERIFY(git(local.path(), {"commit", "-m", "local"}));

        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(local.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.pushTo("origin");
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(),
                 gitBytes(local.path(), {"rev-parse", "HEAD"}).trimmed());
        const auto remoteMain = gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed();

        service.pullFrom("origin", "feature");
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 15000);
        QTRY_VERIFY(!service.busy());
        QVERIFY(QFileInfo::exists(local.filePath("feature.txt")));
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(), remoteMain);
        QVERIFY(errors.isEmpty());
        service.pullFrom("--help", "feature");
        service.pushTo("--help");
        QCOMPARE(errors.size(), 2);
    }

    void syncRebasePullsThenPushesOnlyAfterSuccess() {
        QTemporaryDir seed, remote, client;
        QVERIFY(initialize(seed.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(seed.filePath("base.txt"), "base\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "base"}));
        QVERIFY(git(seed.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(seed.path(), {"push", "--set-upstream", "origin", "main"}));
        QVERIFY(git(client.path(), {"clone", remote.path(), "."}));
        QVERIFY(initialize(client.path()));
        QVERIFY(writeFile(client.filePath("local.txt"), "local\n"));
        QVERIFY(git(client.path(), {"add", "."}));
        QVERIFY(git(client.path(), {"commit", "-m", "local"}));
        QVERIFY(writeFile(seed.filePath("remote.txt"), "remote\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "remote"}));
        QVERIFY(git(seed.path(), {"push", "origin", "main"}));

        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(client.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.sync(true);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QTRY_VERIFY(!service.busy());
        QVERIFY(errors.isEmpty());
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(),
                 gitBytes(client.path(), {"rev-parse", "HEAD"}).trimmed());
        QVERIFY(QFileInfo::exists(client.filePath("remote.txt")));
        const auto parents =
            gitBytes(client.path(), {"show", "-s", "--format=%P", "HEAD"}).trimmed().split(' ');
        QCOMPARE(parents.size(), 1);
    }

    void failedSyncNeverPushesConflictingLocalCommit() {
        QTemporaryDir seed, remote, client;
        QVERIFY(initialize(seed.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(seed.filePath("file.txt"), "base\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "base"}));
        QVERIFY(git(seed.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(seed.path(), {"push", "--set-upstream", "origin", "main"}));
        QVERIFY(git(client.path(), {"clone", remote.path(), "."}));
        QVERIFY(initialize(client.path()));
        QVERIFY(writeFile(client.filePath("file.txt"), "local\n"));
        QVERIFY(git(client.path(), {"add", "."}));
        QVERIFY(git(client.path(), {"commit", "-m", "local"}));
        QVERIFY(writeFile(seed.filePath("file.txt"), "remote\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "remote"}));
        QVERIFY(git(seed.path(), {"push", "origin", "main"}));
        const auto remoteHead = gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed();

        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(client.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.sync(false);
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!errors.isEmpty());
        QCOMPARE(gitBytes(remote.path(), {"rev-parse", "refs/heads/main"}).trimmed(), remoteHead);
    }

    void discardWorkingChangesPreservesStagingAndTrashesUntrackedFiles() {
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        const QString file = directory.filePath(QStringLiteral("file.txt"));
        const QString untracked = directory.filePath(QStringLiteral("untracked.txt"));
        QVERIFY(writeFile(file, "base\n"));
        QVERIFY(git(directory.path(), {"add", "file.txt"}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(writeFile(file, "staged\n"));
        QVERIFY(git(directory.path(), {"add", "file.txt"}));
        QVERIFY(writeFile(file, "working\n"));
        QVERIFY(writeFile(untracked, "keep until confirmed\n"));

        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && service.status().changes.size() == 2);
        service.discardWorkingChanges(false);
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        QFile restored(file);
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), QByteArray("staged\n"));
        restored.close();
        QCOMPARE(gitBytes(directory.path(), {"show", ":file.txt"}), QByteArray("staged\n"));
        QVERIFY(QFileInfo::exists(untracked));

        service.discardWorkingChanges(true);
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!QFileInfo::exists(untracked));
        QVERIFY(errors.isEmpty());
    }

    void initializeLocalRepositoryWithoutRemoteAuthentication() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished);
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && service.gitInstalled());
        QVERIFY(service.repository().isEmpty());
        service.initializeRepository();
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        QCOMPARE(QDir::cleanPath(service.repository()), QDir::cleanPath(directory.path()));
        QVERIFY(QFileInfo(directory.filePath(QStringLiteral(".git"))).isDir());
        QVERIFY(errors.isEmpty());
    }

    void tagCommandsKeepLocalAndRemoteRefsSeparate() {
        QTemporaryDir directory, remote;
        QVERIFY(initialize(directory.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(directory.filePath("file.txt"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(git(directory.path(), {"remote", "add", "origin", remote.path()}));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error),
            remoteTags(&service, &GitService::remoteTagsReady);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());

        service.createTag("v1", "first release");
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(gitBytes(directory.path(), {"cat-file", "-t", "refs/tags/v1"}).trimmed(),
                 QByteArray("tag"));
        service.pushTags("origin");
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(!service.busy());
        service.listRemoteTags("origin");
        QTRY_COMPARE(remoteTags.size(), 1);
        QCOMPARE(remoteTags.takeFirst().at(1).toStringList(), QStringList{"v1"});

        service.deleteTag("v1");
        QTRY_COMPARE(completed.size(), 3);
        QTRY_VERIFY(!service.busy());
        QVERIFY(gitBytes(directory.path(), {"show-ref", "--tags"}).isEmpty());
        service.listRemoteTags("origin");
        QTRY_COMPARE(remoteTags.size(), 1);
        QCOMPARE(remoteTags.takeFirst().at(1).toStringList(), QStringList{"v1"});

        service.deleteRemoteTag("origin", "v1");
        QTRY_COMPARE(completed.size(), 4);
        QTRY_VERIFY(!service.busy());
        service.listRemoteTags("origin");
        QTRY_COMPARE(remoteTags.size(), 1);
        QCOMPARE(remoteTags.takeFirst().at(1).toStringList(), QStringList{});
        QVERIFY(errors.isEmpty());
    }
    void invalidTagNameNeverCreatesARef() {
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("file.txt"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        GitService service;
        QSignalSpy errors(&service, &GitService::error);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.createTag("bad..name");
        QTRY_VERIFY(!errors.isEmpty());
        QVERIFY(gitBytes(directory.path(), {"show-ref", "--tags"}).isEmpty());
    }
    void branchCommandsMergeRenameAndDelete() {
        QTemporaryDir directory, remote;
        QVERIFY(initialize(directory.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(directory.filePath("file.txt"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(git(directory.path(), {"branch", "feature"}));
        QVERIFY(git(directory.path(), {"switch", "feature"}));
        QVERIFY(writeFile(directory.filePath("feature.txt"), "feature\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "feature"}));
        QVERIFY(git(directory.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(directory.path(), {"push", "origin", "feature"}));
        QVERIFY(git(directory.path(), {"switch", "main"}));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.mergeBranch("refs/heads/feature");
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        QVERIFY(QFile::exists(directory.filePath("feature.txt")));
        service.renameCurrentBranch("renamed");
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(gitBytes(directory.path(), {"symbolic-ref", "--short", "HEAD"}).trimmed(),
                 QByteArray("renamed"));
        service.deleteBranch("feature");
        QTRY_COMPARE(completed.size(), 3);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!git(directory.path(), {"show-ref", "--verify", "refs/heads/feature"}));
        service.deleteRemoteBranch("origin", "feature");
        QTRY_COMPARE(completed.size(), 4);
        QTRY_VERIFY(!service.busy());
        QVERIFY(gitBytes(remote.path(), {"show-ref", "--heads"}).isEmpty());
        QVERIFY(errors.isEmpty());
    }
    void stashCommandsPreserveAndRestoreWork() {
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("tracked.txt"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(writeFile(directory.filePath("tracked.txt"), "changed\n"));
        QVERIFY(writeFile(directory.filePath("untracked.txt"), "new\n"));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error), stashes(&service, &GitService::stashesReady),
            patches(&service, &GitService::stashPatchReady);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.stash(true, false, "saved work");
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!QFile::exists(directory.filePath("untracked.txt")));
        service.listStashes();
        QTRY_COMPARE(stashes.size(), 1);
        QCOMPARE(stashes.takeFirst().at(0).toStringList(), QStringList{"stash@{0}"});
        service.viewStash("stash@{0}");
        QTRY_COMPARE(patches.size(), 1);
        QVERIFY(patches.takeFirst().at(1).toString().contains("tracked.txt"));
        service.applyStash("stash@{0}", true);
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(!service.busy());
        QVERIFY(QFile::exists(directory.filePath("untracked.txt")));
        service.listStashes();
        QTRY_COMPARE(stashes.size(), 1);
        QCOMPARE(stashes.takeFirst().at(0).toStringList(), QStringList{});
        QVERIFY(errors.isEmpty());
    }
    void commitAllAndUndoKeepWorkingFiles() {
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("first.txt"), "first\n"));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error), undone(&service, &GitService::commitUndone);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.commitWithOptions("initial", {GitCommitScope::All});
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(gitBytes(directory.path(), {"show", "HEAD:first.txt"}), QByteArray("first\n"));
        service.undoCommit();
        QTRY_COMPARE(undone.size(), 1);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(undone.first().first().toString(), QString("initial"));
        QVERIFY(QFile::exists(directory.filePath("first.txt")));
        QVERIFY(gitBytes(directory.path(), {"rev-parse", "--verify", "HEAD"}).isEmpty());
        QVERIFY(errors.isEmpty());
    }
    void commitOptionsAmendAndSignoff() {
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("file.txt"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(writeFile(directory.filePath("file.txt"), "changed\n"));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.commitWithOptions("amended", {GitCommitScope::All, true, true});
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        const auto message = gitBytes(directory.path(), {"log", "-1", "--format=%B"});
        QVERIFY(message.contains("amended"));
        QVERIFY(message.contains("Signed-off-by: LiteCode Test"));
        QCOMPARE(gitBytes(directory.path(), {"rev-list", "--count", "HEAD"}).trimmed(),
                 QByteArray("1"));
        QVERIFY(errors.isEmpty());
    }
    void autostashConflictKeepsRecoverableChanges() {
        QTemporaryDir seed, remote, client;
        QVERIFY(initialize(seed.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(seed.filePath("file.txt"), "base\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "base"}));
        QVERIFY(git(seed.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(seed.path(), {"push", "--set-upstream", "origin", "main"}));
        QVERIFY(git(client.path(), {"clone", remote.path(), "."}));
        QVERIFY(initialize(client.path()));
        QVERIFY(writeFile(seed.filePath("file.txt"), "remote\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "remote"}));
        QVERIFY(git(seed.path(), {"push", "origin", "main"}));
        QVERIFY(writeFile(client.filePath("file.txt"), "local unsaved work\n"));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error);
        service.setWorkspace(client.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        litecode::core::GitPreferences preferences;
        preferences.autoStash = true;
        preferences.pullPolicy = litecode::core::GitPullPolicy::Merge;
        service.setPreferences(preferences);
        service.pull();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!errors.isEmpty());
        QVERIFY(std::any_of(service.status().changes.cbegin(), service.status().changes.cend(),
                            [](const GitChange& change) { return change.conflict; }));
        QCOMPARE(gitBytes(client.path(), {"show", "refs/stash:file.txt"}),
                 QByteArray("local unsaved work\n"));
    }
    void pullPoliciesAndDivergence_data() {
        QTest::addColumn<int>("policy");
        QTest::newRow("merge-with-autostash") << int(litecode::core::GitPullPolicy::Merge);
        QTest::newRow("rebase-with-autostash") << int(litecode::core::GitPullPolicy::Rebase);
        QTest::newRow("fast-forward-only-refuses-divergence")
            << int(litecode::core::GitPullPolicy::FastForwardOnly);
    }
    void pullPoliciesAndDivergence() {
        QFETCH(int, policy);
        QTemporaryDir seed, remote, client;
        QVERIFY(initialize(seed.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(seed.filePath("dirty.txt"), "base\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "base"}));
        const auto base = gitBytes(seed.path(), {"rev-parse", "HEAD"}).trimmed();
        QVERIFY(git(seed.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(seed.path(), {"push", "--set-upstream", "origin", "main"}));
        QVERIFY(git(client.path(), {"clone", remote.path(), "."}));
        QVERIFY(initialize(client.path()));
        QVERIFY(writeFile(client.filePath("local.txt"), "local\n"));
        QVERIFY(git(client.path(), {"add", "."}));
        QVERIFY(git(client.path(), {"commit", "-m", "local"}));
        const auto originalHead = gitBytes(client.path(), {"rev-parse", "HEAD"}).trimmed();
        QVERIFY(writeFile(seed.filePath("remote.txt"), "remote\n"));
        QVERIFY(git(seed.path(), {"add", "."}));
        QVERIFY(git(seed.path(), {"commit", "-m", "remote"}));
        QVERIFY(git(seed.path(), {"push", "origin", "main"}));
        QVERIFY(writeFile(client.filePath("dirty.txt"), "uncommitted work\n"));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error),
            divergence(&service, &GitService::divergenceReady);
        service.setWorkspace(client.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.fetch();
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        service.listDivergence();
        QTRY_COMPARE(divergence.size(), 1);
        QTRY_VERIFY(!service.busy());
        const auto counts = qvariant_cast<GitDivergence>(divergence.first()[0]);
        QCOMPARE(counts.incoming, 1);
        QCOMPARE(counts.outgoing, 1);
        QCOMPARE(counts.mergeBase.toUtf8(), base);
        litecode::core::GitPreferences preferences;
        preferences.autoStash = true;
        preferences.fetchOnPull = true;
        preferences.pullPolicy = static_cast<litecode::core::GitPullPolicy>(policy);
        service.setPreferences(preferences);
        service.pull();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 15000);
        QTRY_VERIFY(!service.busy());
        QFile dirty(client.filePath("dirty.txt"));
        QVERIFY(dirty.open(QIODevice::ReadOnly));
        QCOMPARE(dirty.readAll(), QByteArray("uncommitted work\n"));
        if (policy == int(litecode::core::GitPullPolicy::FastForwardOnly)) {
            QVERIFY(!errors.isEmpty());
            QCOMPARE(gitBytes(client.path(), {"rev-parse", "HEAD"}).trimmed(), originalHead);
        } else {
            QCOMPARE(errors.size(), 0);
            QVERIFY(QFile::exists(client.filePath("remote.txt")));
            QVERIFY(QFile::exists(client.filePath("local.txt")));
            const auto parents =
                gitBytes(client.path(), {"show", "-s", "--format=%P", "HEAD"}).trimmed().split(' ');
            QCOMPARE(parents.size(), policy == int(litecode::core::GitPullPolicy::Merge) ? 2 : 1);
        }
    }
    void failedCheckoutRestoresWorkAndMigrationConflictRetainsBackup() {
        QTemporaryDir directory, otherWorktree;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("work.txt"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(git(directory.path(), {"branch", "feature"}));
        QVERIFY(git(directory.path(), {"worktree", "add", otherWorktree.path(), "feature"}));
        QVERIFY(writeFile(directory.filePath("work.txt"), "staged\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(writeFile(directory.filePath("work.txt"), "unstaged\n"));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error),
            backups(&service, &GitService::recoveryCopyCreated);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.checkoutBranch("refs/heads/feature", GitCheckoutRecovery::Stash);
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!errors.isEmpty());
        QCOMPARE(service.status().branch, QString("main"));
        QCOMPARE(gitBytes(directory.path(), {"show", ":work.txt"}), QByteArray("staged\n"));
        QFile work(directory.filePath("work.txt"));
        QVERIFY(work.open(QIODevice::ReadOnly));
        QCOMPARE(work.readAll(), QByteArray("unstaged\n"));
        work.close();
        QCOMPARE(backups.size(), 1);
        QVERIFY(git(directory.path(), {"show-ref", "--verify", backups.last()[0].toString()}));
        // Change destination independently in its worktree, then unlock it by detaching.
        QVERIFY(writeFile(otherWorktree.filePath("work.txt"), "destination\n"));
        QVERIFY(git(otherWorktree.path(), {"add", "."}));
        QVERIFY(git(otherWorktree.path(),
                    {"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit",
                     "-m", "destination"}));
        QVERIFY(git(otherWorktree.path(), {"checkout", "--detach"}));
        service.checkoutBranch("refs/heads/feature", GitCheckoutRecovery::Migrate);
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(service.status().branch, QString("feature"));
        QCOMPARE(backups.size(), 2);
        QVERIFY(errors.size() >= 2);
        const auto backup = backups.last()[0].toString();
        QCOMPARE(gitBytes(directory.path(), {"show", backup + ":work.txt"}),
                 QByteArray("unstaged\n"));
        QCOMPARE(gitBytes(directory.path(), {"show", backup + "^2:work.txt"}),
                 QByteArray("staged\n"));
    }
    void fetchScopePruningAndTagBackups() {
        QTemporaryDir directory, origin, secondary;
        QVERIFY(initialize(directory.path()));
        for (const auto& remote : {origin.path(), secondary.path()})
            QVERIFY(git(remote, {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(directory.filePath("file.txt"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(git(directory.path(), {"-c", "tag.gpgsign=false", "tag", "-a", "v1", "-m", "old"}));
        const auto oldTag = gitBytes(directory.path(), {"rev-parse", "refs/tags/v1"}).trimmed();
        QVERIFY(!oldTag.isEmpty());
        QVERIFY(git(directory.path(), {"remote", "add", "origin", origin.path()}));
        QVERIFY(git(directory.path(), {"remote", "add", "secondary", secondary.path()}));
        QVERIFY(git(directory.path(),
                    {"push", "--set-upstream", "origin", "main", "refs/tags/v1", "main:obsolete"}));
        QVERIFY(git(directory.path(), {"branch", "--set-upstream-to=origin/main", "main"}));
        QVERIFY(git(directory.path(), {"push", "secondary", "main:other"}));
        QVERIFY(git(directory.path(), {"update-ref", "-d", "refs/remotes/secondary/other"}));
        QVERIFY(git(origin.path(), {"update-ref", "-d", "refs/heads/obsolete"}));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error), conflicts(&service, &GitService::tagConflict),
            backups(&service, &GitService::tagBackupCreated);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.fetch();
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!git(directory.path(),
                     {"show-ref", "--verify", "--quiet", "refs/remotes/secondary/other"}));
        QVERIFY(git(directory.path(),
                    {"show-ref", "--verify", "--quiet", "refs/remotes/origin/obsolete"}));
        service.fetch({}, true);
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(!service.busy());
        QVERIFY(git(directory.path(),
                    {"show-ref", "--verify", "--quiet", "refs/remotes/secondary/other"}));
        service.fetch("origin", false, true);
        QTRY_COMPARE(completed.size(), 3);
        QTRY_VERIFY(!service.busy());
        QVERIFY(!git(directory.path(),
                     {"show-ref", "--verify", "--quiet", "refs/remotes/origin/obsolete"}));
        QCOMPARE(errors.size(), 0);
        QVERIFY(writeFile(directory.filePath("file.txt"), "new\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "new"}));
        QVERIFY(git(directory.path(),
                    {"-c", "tag.gpgsign=false", "tag", "-f", "-a", "v1", "-m", "new"}));
        const auto newTag = gitBytes(directory.path(), {"rev-parse", "refs/tags/v1"}).trimmed();
        QVERIFY(git(directory.path(), {"push", "origin", "main", "+refs/tags/v1:refs/tags/v1"}));
        QVERIFY(git(directory.path(), {"update-ref", "refs/tags/v1", QString::fromUtf8(oldTag)}));
        service.pull();
        QTRY_COMPARE(completed.size(), 4);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(gitBytes(directory.path(), {"rev-parse", "refs/tags/v1"}).trimmed(), oldTag);
        service.replaceRemoteTag("origin", "refs/tags/v1");
        QTRY_COMPARE(completed.size(), 5);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(backups.size(), 1);
        const auto backup = backups.first()[0].toString();
        QCOMPARE(gitBytes(directory.path(), {"rev-parse", backup}).trimmed(), oldTag);
        QCOMPARE(gitBytes(directory.path(), {"rev-parse", "refs/tags/v1"}).trimmed(), newTag);
        service.restoreTagBackup(backup);
        QTRY_COMPARE(completed.size(), 6);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(backups.size(), 2);
        QCOMPARE(gitBytes(directory.path(), {"rev-parse", "refs/tags/v1"}).trimmed(), oldTag);
        QCOMPARE(gitBytes(directory.path(), {"rev-parse", backups.last()[0].toString()}).trimmed(),
                 newTag);
        // Simulate another Git client changing the tag after our backup and
        // before the fetched replacement is installed. CAS must refuse it.
        bool externalChange = false;
        connect(&service, &GitService::tagBackupCreated, &service, [&](const QString&) {
            externalChange =
                git(directory.path(), {"update-ref", "refs/tags/v1", QString::fromUtf8(newTag)});
        });
        const auto priorErrors = errors.size();
        service.replaceRemoteTag("origin", "refs/tags/v1");
        QTRY_COMPARE(completed.size(), 7);
        QTRY_VERIFY(!service.busy());
        QVERIFY(externalChange);
        QVERIFY(errors.size() > priorErrors);
        QCOMPARE(gitBytes(directory.path(), {"rev-parse", "refs/tags/v1"}).trimmed(), newTag);
        QCOMPARE(gitBytes(directory.path(), {"rev-parse", backups.last()[0].toString()}).trimmed(),
                 oldTag);
    }
    void repositoryDiscoveryAndHistoryFilters() {
        QTemporaryDir directory;
        const auto first = directory.filePath("first"),
                   second = directory.filePath("nested/second");
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        QVERIFY(initialize(first));
        QVERIFY(initialize(second));
        QCOMPARE(discoverGitRepositories(directory.path()).size(), 2);
        QCOMPARE(discoverGitRepositories(directory.path(), 1).size(), 1);
        QVERIFY(writeFile(QDir(first).filePath("file.txt"), "base\n"));
        QVERIFY(git(first, {"add", "."}));
        QVERIFY(git(first, {"commit", "-m", "base"}));
        QVERIFY(git(first, {"checkout", "-b", "unrelated"}));
        QVERIFY(writeFile(QDir(first).filePath("file.txt"), "other\n"));
        QVERIFY(git(first, {"add", "."}));
        QVERIFY(git(first, {"commit", "-m", "other"}));
        QVERIFY(git(first, {"checkout", "main"}));
        GitService service;
        QSignalSpy history(&service, &GitService::historyReady);
        service.setWorkspace(first);
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.listHistory(100, {}, true);
        QTRY_COMPARE(history.size(), 1);
        QCOMPARE(qvariant_cast<QVector<GitCommit>>(history.last()[0]).size(), 1);
        service.listHistory();
        QTRY_COMPARE(history.size(), 2);
        QCOMPARE(qvariant_cast<QVector<GitCommit>>(history.last()[0]).size(), 2);
        service.listHistory(100, {"refs/heads/main"});
        QTRY_COMPARE(history.size(), 3);
        QCOMPARE(qvariant_cast<QVector<GitCommit>>(history.last()[0]).size(), 1);
    }
    void checkoutRecoveryPreservesEveryLayer_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("stash") << int(GitCheckoutRecovery::Stash);
        QTest::newRow("migrate") << int(GitCheckoutRecovery::Migrate);
        QTest::newRow("backup-including-ignored") << int(GitCheckoutRecovery::BackupAll);
    }
    void checkoutRecoveryPreservesEveryLayer() {
        QFETCH(int, mode);
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("work.txt"), "base\n"));
        QVERIFY(writeFile(directory.filePath("branch.txt"), "main\n"));
        QVERIFY(writeFile(directory.filePath(".gitignore"), "ignored.txt\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(git(directory.path(), {"checkout", "-b", "feature"}));
        QVERIFY(writeFile(directory.filePath("branch.txt"), "feature\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "feature"}));
        QVERIFY(git(directory.path(), {"checkout", "main"}));
        QVERIFY(writeFile(directory.filePath("work.txt"), "staged\n"));
        QVERIFY(git(directory.path(), {"add", "work.txt"}));
        QVERIFY(writeFile(directory.filePath("work.txt"), "working\n"));
        QVERIFY(writeFile(directory.filePath("untracked.txt"), "untracked recovery\n"));
        QVERIFY(writeFile(directory.filePath("ignored.txt"), "ignored recovery\n"));
        GitService service;
        QSignalSpy copies(&service, &GitService::recoveryCopyCreated),
            errors(&service, &GitService::error),
            completed(&service, &GitService::operationFinished);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.checkoutBranch("refs/heads/feature", static_cast<GitCheckoutRecovery>(mode));
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy() && service.status().branch == "feature");
        QCOMPARE(errors.size(), 0);
        QCOMPARE(copies.size(), 1);
        const auto recovery = copies.first()[0].toString();
        QVERIFY(git(directory.path(), {"show-ref", "--verify", recovery}));
        if (mode != int(GitCheckoutRecovery::Migrate)) {
            QVERIFY(!QFile::exists(directory.filePath("untracked.txt")));
            QCOMPARE(QFile::exists(directory.filePath("ignored.txt")),
                     mode != int(GitCheckoutRecovery::BackupAll));
            service.restoreRecoveryCopy(recovery);
            QTRY_COMPARE(completed.size(), 2);
            QTRY_VERIFY(!service.busy());
            QCOMPARE(errors.size(), 0);
        }
        for (const auto& pair :
             {qMakePair(QString("work.txt"), QByteArray("working\n")),
              qMakePair(QString("untracked.txt"), QByteArray("untracked recovery\n")),
              qMakePair(QString("ignored.txt"), QByteArray("ignored recovery\n"))}) {
            QFile file(directory.filePath(pair.first));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), pair.second);
        }
        QProcess index;
        index.setWorkingDirectory(directory.path());
        index.start("git", {"show", ":work.txt"});
        QVERIFY(index.waitForFinished(10000));
        QCOMPARE(index.exitCode(), 0);
        QCOMPARE(index.readAllStandardOutput(), QByteArray("staged\n"));
        QVERIFY(git(directory.path(), {"show-ref", "--verify", recovery})); // Never auto-deleted.
    }
    void liveBranchValidationAndRevisionCheckout() {
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(git(directory.path(), {"tag", "v1"}));
        GitService service;
        QSignalSpy validated(&service, &GitService::branchNameValidated),
            errors(&service, &GitService::error);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.validateBranchName("main");
        QTRY_COMPARE(validated.size(), 1);
        QVERIFY(!validated.last()[1].toString().isEmpty());
        service.validateBranchName("feature/test");
        QTRY_COMPARE(validated.size(), 2);
        QVERIFY(validated.last()[1].toString().isEmpty());
        for (const auto& name :
             {QString("bad..name"), QString("@{-1}"), QString("--help"), QString("HEAD")}) {
            const auto count = validated.size();
            service.validateBranchName(name);
            QTRY_COMPARE(validated.size(), count + 1);
            QVERIFY(!validated.last()[1].toString().isEmpty());
        }
        QCOMPARE(GitService::sanitizeBranchName("  --hello world  "), QString("hello-world"));
        QCOMPARE(GitService::sanitizeBranchName("new branch", "_"), QString("new_branch"));
        service.createBranch("from-tag", "refs/tags/v1");
        QTRY_VERIFY(!service.busy() && service.status().branch == "from-tag");
        service.checkoutDetached("refs/tags/v1");
        QTRY_VERIFY(!service.busy() && service.status().branch.contains("HEAD"));
        QCOMPARE(errors.size(), 0);
        service.checkoutBranch("refs/heads/main");
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.checkoutBranch("refs/tags/v1");
        QTRY_VERIFY(!service.busy() && service.status().branch.contains("HEAD"));
        service.createBranch("invalid-base", "--help");
        QCOMPARE(errors.size(), 1);
    }
    void commitHistorySnapshots() {
        QTemporaryDir directory;
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "first"}));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "second\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "second"}));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "dirty working file\n"));
        GitService service;
        QSignalSpy history(&service, &GitService::historyReady);
        QSignalSpy files(&service, &GitService::commitFilesReady);
        QSignalSpy comparisons(&service, &GitService::comparisonReady);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        service.listHistory();
        QTRY_COMPARE(history.size(), 1);
        const auto commits = qvariant_cast<QVector<GitCommit>>(history.first()[0]);
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits[0].subject, QString("second"));
        QCOMPARE(commits[0].parents, QStringList{commits[1].id});
        QVERIFY(commits[1].parents.isEmpty());
        service.listCommitFiles(commits[0].id, commits[1].id);
        QTRY_COMPARE(files.size(), 1);
        const auto changed = qvariant_cast<QVector<GitCommitFile>>(files.first()[1]);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed[0].path, QString("file.cpp"));
        service.compareCommitFile(commits[0].id, commits[1].id, changed[0]);
        QTRY_COMPARE(comparisons.size(), 1);
        auto comparison = qvariant_cast<GitComparison>(comparisons.first()[0]);
        QVERIFY(comparison.historical);
        QCOMPARE(comparison.original, QByteArray("base\n"));
        QCOMPARE(comparison.modified, QByteArray("second\n"));
        service.listCommitFiles(commits[1].id, {});
        QTRY_COMPARE(files.size(), 2);
        const auto initial = qvariant_cast<QVector<GitCommitFile>>(files.last()[1]);
        QCOMPARE(initial.size(), 1);
        QCOMPARE(initial[0].status, 'A');
        service.compareCommitFile(commits[1].id, {}, initial[0]);
        QTRY_COMPARE(comparisons.size(), 2);
        comparison = qvariant_cast<GitComparison>(comparisons.last()[0]);
        QVERIFY(comparison.original.isEmpty());
        QCOMPARE(comparison.modified, QByteArray("base\n"));
        QFile working(directory.filePath("file.cpp"));
        QVERIFY(working.open(QIODevice::ReadOnly));
        QCOMPARE(working.readAll(), QByteArray("dirty working file\n"));
    }
    void referenceParsers() {
        QByteArray rows;
        auto row = [&rows](const QList<QByteArray>& fields) { rows += fields.join('\0') + '\n'; };
        row({"refs/heads/main", "1234", "*", "origin/main", ""});
        row({"refs/heads/feature/name", "abcd", " ", "", ""});
        row({"refs/remotes/origin/main", "1234", " ", "", ""});
        row({"refs/remotes/origin/HEAD", "1234", " ", "", "refs/remotes/origin/main"});
        const auto branches = GitService::parseBranches(rows);
        QCOMPARE(branches.size(), 3);
        QVERIFY(branches[0].current);
        QCOMPARE(branches[0].upstream, QString("origin/main"));
        QVERIFY(branches[2].remote);
        QCOMPARE(branches[2].name, QString("origin/main"));
        const auto remotes = GitService::parseRemotes(
            "origin\tC:/repo with space.git (fetch)\norigin\tC:/other.git (push)\n");
        QCOMPARE(remotes.size(), 1);
        QCOMPARE(remotes[0].name, QString("origin"));
        QCOMPARE(remotes[0].fetchUrl, QString("C:/repo with space.git"));
        QCOMPARE(remotes[0].pushUrl, QString("C:/other.git"));
    }
    void branchesPreserveConflictingChanges() {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("Git not installed");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(initialize(directory.path()));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "--", "file.cpp"}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        GitService service;
        QSignalSpy errors(&service, &GitService::error),
            listed(&service, &GitService::branchesReady);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.createBranch("feature/test");
        QTRY_VERIFY(!service.busy() && service.status().branch == "feature/test");
        service.listBranches();
        QTRY_COMPARE(listed.size(), 1);
        const auto branches = qvariant_cast<QVector<GitBranch>>(listed.first()[0]);
        QCOMPARE(branches.size(), 2);
        QVERIFY(std::any_of(branches.begin(), branches.end(), [](const auto& branch) {
            return branch.current && branch.ref == "refs/heads/feature/test";
        }));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "feature\n"));
        QVERIFY(git(directory.path(), {"add", "--", "file.cpp"}));
        QVERIFY(git(directory.path(), {"commit", "-m", "feature"}));
        service.checkoutBranch("refs/heads/main");
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        QVERIFY(writeFile(directory.filePath("file.cpp"), "unsaved work on disk\n"));
        service.checkoutBranch("refs/heads/feature/test");
        QTRY_COMPARE(errors.size(), 1);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(service.status().branch, QString("main"));
        QFile file(directory.filePath("file.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("unsaved work on disk\n"));
        service.createBranch("--help");
        QCOMPARE(errors.size(), 2);
        service.checkoutBranch("refs/heads/main~1");
        QTRY_COMPARE(errors.size(), 3);
        QCOMPARE(service.status().branch, QString("main"));
    }
    void localRemotePublishFetchPull() {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("Git not installed");
        QTemporaryDir directory, remote, peer;
        QVERIFY(directory.isValid() && remote.isValid() && peer.isValid());
        QVERIFY(initialize(directory.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "--", "file.cpp"}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        GitService service;
        QSignalSpy completed(&service, &GitService::operationFinished),
            errors(&service, &GitService::error);
        QSignalSpy listed(&service, &GitService::remotesReady);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.addRemote("origin", remote.path());
        QTRY_COMPARE(completed.size(), 1);
        QTRY_VERIFY(!service.busy());
        service.listRemotes();
        QTRY_COMPARE(listed.size(), 1);
        const auto remotes = qvariant_cast<QVector<GitRemote>>(listed.first()[0]);
        QCOMPARE(remotes.size(), 1);
        QCOMPARE(remotes[0].fetchUrl, remote.path());
        service.push("origin", true);
        QTRY_COMPARE(completed.size(), 2);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(errors.size(), 0);
        QVERIFY(service.status().branch.startsWith("main...origin/main"));
        QVERIFY(git(peer.path(), {"clone", remote.path(), "."}));
        QVERIFY(git(peer.path(), {"config", "user.name", "Peer Test"}));
        QVERIFY(git(peer.path(), {"config", "user.email", "peer@example.invalid"}));
        QVERIFY(git(peer.path(), {"config", "commit.gpgsign", "false"}));
        QVERIFY(git(peer.path(), {"config", "core.autocrlf", "false"}));
        QVERIFY(writeFile(peer.filePath("file.cpp"), "remote change\n"));
        QVERIFY(git(peer.path(), {"add", "--", "file.cpp"}));
        QVERIFY(git(peer.path(), {"commit", "-m", "remote change"}));
        QVERIFY(git(peer.path(), {"push"}));
        service.fetch("origin");
        QTRY_COMPARE(completed.size(), 3);
        QTRY_VERIFY(!service.busy());
        QVERIFY(service.status().branch.contains("behind 1"));
        service.pull();
        QTRY_COMPARE(completed.size(), 4);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(errors.size(), 0);
        QFile file(directory.filePath("file.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("remote change\n"));
        file.close();
        service.removeRemote("origin");
        QTRY_COMPARE(completed.size(), 5);
        QTRY_VERIFY(!service.busy());
        service.listRemotes();
        QTRY_COMPARE(listed.size(), 2);
        QVERIFY(qvariant_cast<QVector<GitRemote>>(listed.last()[0]).isEmpty());
        service.addRemote("--help", remote.path());
        QCOMPARE(errors.size(), 1);
        service.push({}, true);
        QCOMPARE(errors.size(), 2);
    }
    void remoteCheckoutReusesTrackingBranchAndPushesUpstream() {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("Git not installed");
        QTemporaryDir directory, remote;
        QVERIFY(initialize(directory.path()));
        QVERIFY(git(remote.path(), {"init", "--bare", "--initial-branch=main"}));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "base\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "base"}));
        QVERIFY(git(directory.path(), {"remote", "add", "origin", remote.path()}));
        QVERIFY(git(directory.path(), {"push", "origin", "main:release"}));
        GitService service;
        QSignalSpy errors(&service, &GitService::error),
            publish(&service, &GitService::publishRequired),
            completed(&service, &GitService::operationFinished);
        service.setWorkspace(directory.path());
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.push();
        QTRY_COMPARE(publish.size(), 1);
        QTRY_VERIFY(!service.busy());
        service.checkoutBranch("refs/remotes/origin/release");
        QTRY_VERIFY(!service.busy() &&
                    service.status().branch.startsWith("release...origin/release"));
        service.checkoutBranch("refs/heads/main");
        QTRY_VERIFY(!service.busy() && service.status().branch == "main");
        service.checkoutBranch("refs/remotes/origin/release");
        QTRY_VERIFY(!service.busy() &&
                    service.status().branch.startsWith("release...origin/release"));
        QCOMPARE(errors.size(), 0);
        // Upstream names can differ. VS Code pushes this branch to its configured
        // upstream rather than relying on Git's push.default=simple.
        QVERIFY(git(directory.path(), {"branch", "-m", "local-release"}));
        QVERIFY(git(directory.path(), {"config", "push.default", "simple"}));
        QVERIFY(writeFile(directory.filePath("file.cpp"), "new change\n"));
        QVERIFY(git(directory.path(), {"add", "."}));
        QVERIFY(git(directory.path(), {"commit", "-m", "change"}));
        const int before = completed.size();
        service.push();
        QTRY_COMPARE(completed.size(), before + 1);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(errors.size(), 0);
        QVERIFY(!service.status().branch.contains("ahead"));
        QProcess verify;
        verify.setWorkingDirectory(remote.path());
        verify.start("git", {"show", "refs/heads/release:file.cpp"});
        QVERIFY(verify.waitForFinished(10000));
        QCOMPARE(verify.exitCode(), 0);
        QCOMPARE(verify.readAllStandardOutput(), QByteArray("new change\n"));
    }
    void parse() {
        QByteArray data("## main");
        data += '\0';
        data += "RM new name.cpp";
        data += '\0';
        data += "old name.cpp";
        data += '\0';
        data += "UU conflict.cpp";
        data += '\0';
        data += "?? line\nbreak.cpp";
        data += '\0';
        const auto result = GitService::parseStatus(data);
        QCOMPARE(result.branch, QString("main"));
        QCOMPARE(result.changes.size(), 3);
        QCOMPARE(result.changes[0].originalPath, QString("old name.cpp"));
        QCOMPARE(result.changes[0].worktree, 'M');
        QVERIFY(result.changes[1].conflict);
        QCOMPARE(result.changes[2].path, QString("line\nbreak.cpp"));
    }
    void repositoryWorkflow() {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("Git not installed");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto run = [&](const QStringList& args) {
            QProcess process;
            process.setWorkingDirectory(directory.path());
            process.start("git", args);
            return process.waitForFinished(10000) && process.exitCode() == 0;
        };
        QVERIFY(run({"init"}));
        QVERIFY(run({"config", "user.name", "LiteCode Test"}));
        QVERIFY(run({"config", "user.email", "test@example.invalid"}));
        QVERIFY(run({"config", "commit.gpgsign", "false"}));
        QFile file(directory.filePath("[literal] space.cpp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("one\n");
        file.close();
        GitService service;
        QSignalSpy changed(&service, &GitService::changed), errors(&service, &GitService::error);
        service.setWorkspace(directory.path());
        QTRY_COMPARE(service.status().changes.size(), 1);
        service.stage({"[literal] space.cpp"});
        QTRY_VERIFY(!service.busy() && service.status().changes[0].index == 'A');
        service.unstage({"[literal] space.cpp"});
        QTRY_VERIFY(!service.busy() && service.status().changes[0].index == '?');
        service.stage({"[literal] space.cpp"});
        QTRY_VERIFY(!service.busy() && service.status().changes[0].index == 'A');
        service.commit("Initial test commit");
        QTRY_VERIFY(!service.busy() && service.status().changes.isEmpty());
        QVERIFY(file.open(QIODevice::Append));
        file.write("two\n");
        file.close();
        service.refresh();
        QTRY_COMPARE(service.status().changes.size(), 1);
        QSignalSpy diffs(&service, &GitService::diffReady);
        service.diff("[literal] space.cpp", false);
        QTRY_COMPARE(diffs.size(), 1);
        QVERIFY(diffs[0][1].toString().contains("+two"));
        QSignalSpy comparisons(&service, &GitService::comparisonReady);
        service.compare("[literal] space.cpp", false);
        QTRY_COMPARE(comparisons.size(), 1);
        auto comparison = qvariant_cast<GitComparison>(comparisons.last()[0]);
        QCOMPARE(comparison.original, QByteArray("one\n"));
        QCOMPARE(comparison.modified, QByteArray("one\ntwo\n"));
        service.stage({"[literal] space.cpp"});
        QTRY_VERIFY(!service.busy() && service.status().changes[0].index == 'M');
        QVERIFY(file.open(QIODevice::Append));
        file.write("three\n");
        file.close();
        service.refresh();
        QTRY_VERIFY(!service.busy() && service.status().changes[0].worktree == 'M');
        service.compare("[literal] space.cpp", true);
        QTRY_COMPARE(comparisons.size(), 2);
        comparison = qvariant_cast<GitComparison>(comparisons.last()[0]);
        QCOMPARE(comparison.original, QByteArray("one\n"));
        QCOMPARE(comparison.modified, QByteArray("one\ntwo\n"));
        service.compare("[literal] space.cpp", false);
        QTRY_COMPARE(comparisons.size(), 3);
        comparison = qvariant_cast<GitComparison>(comparisons.last()[0]);
        QCOMPARE(comparison.original, QByteArray("one\ntwo\n"));
        QCOMPARE(comparison.modified, QByteArray("one\ntwo\nthree\n"));
        service.stage({"../outside"});
        QCOMPARE(errors.size(), 1);
        QCOMPARE(service.status().changes[0].index, 'M');
        service.setWorkspace({});
        QCOMPARE(service.repository(), QString());
        QVERIFY(service.status().changes.isEmpty());
    }
};
QTEST_GUILESS_MAIN(GitServiceTests)
#include "tst_git_service.moc"
