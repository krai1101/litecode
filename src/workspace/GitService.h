#pragma once
#include "core/GitPreferences.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <functional>
#include <optional>

namespace litecode::workspace {
enum class GitCheckoutRecovery { None, Stash, Migrate, BackupAll };
struct GitChange {
    QString path, originalPath;
    char index{' '}, worktree{' '};
    bool conflict{};
};
struct GitStatus {
    QString branch;
    QVector<GitChange> changes;
};
struct GitComparison {
    QString path, originalPath, repository;
    QByteArray original, modified;
    bool staged{}, binary{}, modifiedExists{true};
    QString originalRevision, modifiedRevision;
    bool historical{};
};
struct GitCommit {
    QString id, subject, author, references;
    QStringList parents;
    qint64 timestamp{};
};
struct GitCommitFile {
    QString path, originalPath;
    char status{'M'};
};
struct GitDivergence {
    QString head, upstream, mergeBase;
    int incoming{}, outgoing{};
};
struct GitBranch {
    QString name, ref, commit, upstream;
    bool current{}, remote{};
    bool tag{};
};
struct GitRemote {
    QString name, fetchUrl, pushUrl;
};
enum class GitCommitScope { Staged, All };
struct GitCommitOptions {
    GitCommitScope scope{GitCommitScope::Staged};
    bool amend{}, signoff{}, noVerify{};
};
class GitService final : public QObject {
    Q_OBJECT
  public:
    explicit GitService(QObject* parent = nullptr);
    ~GitService() override;
    void setWorkspace(const QString& path);
    QString repository() const { return repository_; }
    bool busy() const { return busy_; }
    bool gitInstalled() const { return gitInstalled_; }
    bool supportsStagedStash() const;
    quint64 workspaceGeneration() const { return epoch_; }
    const GitStatus& status() const { return status_; }
    void setPreferences(const core::GitPreferences& preferences) { preferences_ = preferences; }
    void refresh();
    void initializeRepository();
    void cloneRepository(const QString& url, const QString& parentDirectory);
    void cancelClone();
    void createWorktree(const QString& path, const QString& ref, const QString& newBranch = {});
    void listWorktrees();
    void deleteWorktree(const QString& path);
    void stage(const QStringList& paths);
    void unstage(const QStringList& paths);
    void discardWorkingChanges(bool includeUntracked = true);
    void commit(const QString& message);
    void commitWithOptions(const QString& message, GitCommitOptions options);
    void undoCommit();
    void abortRebase();
    void diff(const QString& path, bool staged);
    void compare(const QString& path, bool staged);
    void listBranches();
    void createBranch(const QString& name, const QString& startPoint = {});
    void mergeBranch(const QString& ref);
    void rebaseBranch(const QString& ref);
    void renameCurrentBranch(const QString& name);
    void deleteBranch(const QString& name);
    void deleteRemoteBranch(const QString& remote, const QString& name);
    void validateBranchName(const QString& name);
    static QString sanitizeBranchName(const QString& name, const QString& whitespace = "-");
    void checkoutBranch(const QString& ref,
                        GitCheckoutRecovery recovery = GitCheckoutRecovery::None);
    void checkoutDetached(const QString& ref,
                          GitCheckoutRecovery recovery = GitCheckoutRecovery::None);
    void listRecoveryCopies();
    void restoreRecoveryCopy(const QString& ref);
    void replaceRemoteTag(const QString& remote, const QString& tagRef);
    void listTagBackups();
    void restoreTagBackup(const QString& backupRef);
    void createTag(const QString& name, const QString& message = {});
    void deleteTag(const QString& name);
    void listRemoteTags(const QString& remote);
    void deleteRemoteTag(const QString& remote, const QString& name);
    void pushTags(const QString& remote);
    void stash(bool includeUntracked = false, bool stagedOnly = false, const QString& message = {});
    void listStashes();
    void applyStash(const QString& ref = "stash@{0}", bool pop = false);
    void dropStash(const QString& ref);
    void dropAllStashes();
    void viewStash(const QString& ref);
    void listRemotes();
    void addRemote(const QString& name, const QString& url);
    void removeRemote(const QString& name);
    void fetch(const QString& remote = {}, bool all = false, bool prune = false);
    void pull();
    void pullRebase();
    void pullFrom(const QString& remote, const QString& branch);
    void sync(bool rebase = false);
    void pushTo(const QString& remote);
    void push(const QString& remote = {}, bool setUpstream = false);
    void pushForce();
    void pushToForce(const QString& remote);
    void listHistory(int limit = 100, const QStringList& references = {}, bool automatic = false);
    void listDivergence();
    void listCommitFiles(const QString& commit, const QString& parent);
    void compareCommitFile(const QString& commit, const QString& parent, const GitCommitFile& file);
    static QVector<GitCommit> parseHistory(const QByteArray& bytes);
    static QVector<GitCommitFile> parseCommitFiles(const QByteArray& bytes);
    static QVector<GitBranch> parseBranches(const QByteArray& bytes);
    static QVector<GitRemote> parseRemotes(const QByteArray& bytes);
    static GitStatus parseStatus(const QByteArray& bytes);
  signals:
    void changed();
    void busyChanged(bool busy);
    void error(const QString& message);
    void diffReady(const QString& path, const QString& diff, bool staged);
    void comparisonReady(const litecode::workspace::GitComparison& comparison);
    void operationFinished();
    void commitSucceeded();
    void commitUndone(const QString& message);
    void publishRequired();
    void branchesReady(const QVector<litecode::workspace::GitBranch>& branches);
    void branchNameValidated(const QString& name, const QString& diagnostic);
    void recoveryCopyCreated(const QString& ref);
    void recoveryCopiesReady(const QStringList& refs);
    void tagConflict(const QString& repository, const QString& diagnostic);
    void tagBackupCreated(const QString& backupRef);
    void tagBackupsReady(const QStringList& refs);
    void remoteTagsReady(const QString& remote, const QStringList& names);
    void stashesReady(const QStringList& refs, const QStringList& descriptions);
    void stashPatchReady(const QString& ref, const QString& patch);
    void checkoutFailed(const QString& repository, const QString& ref, bool detached,
                        const QString& diagnostic);
    void remotesReady(const QVector<litecode::workspace::GitRemote>& remotes);
    void historyReady(const QVector<litecode::workspace::GitCommit>& commits);
    void divergenceReady(const litecode::workspace::GitDivergence& divergence);
    void revisionFilesReady(const QString& commit, const QString& parent,
                            const QVector<litecode::workspace::GitCommitFile>& files);
    void commitFilesReady(const QString& commit,
                          const QVector<litecode::workspace::GitCommitFile>& files);
    void repositoryCloned(const QString& path);
    void cloneCancelled(const QString& destination);
    void worktreeCreated(const QString& path);
    void worktreesReady(const QStringList& paths);
    void worktreeDeleted(const QString& path);
    void outputLine(const QString& line);

  private:
    using Done = std::function<void(int, const QByteArray&, const QString&)>;
    void run(const QString& directory, const QStringList& arguments, Done done);
    void mutate(const QStringList& arguments);
    bool validPaths(const QStringList& paths) const;
    QProcess process_;
    QString currentOperation_;
    std::optional<bool> stagedStashSupported_;
    bool cloneCancellationRequested_{};
    QTimer timeout_;
    QString workspace_, repository_;
    GitStatus status_;
    bool busy_{}, timedOut_{};
    bool gitInstalled_{};
    quint64 epoch_{};
    QString authenticationOperation_;
    QByteArray output_, diagnostic_;
    Done done_;
    core::GitPreferences preferences_;
    void finish(int code, const QString& diagnostic);
    void checkoutCommand(const QStringList& arguments, const QString& ref, bool detached,
                         GitCheckoutRecovery recovery);
    void pullWithMode(std::optional<bool> rebase, bool pushAfter, const QString& remote = {},
                      const QString& branch = {});
    void pushWithOptions(const QString& remote, bool setUpstream, bool force);
    void pushToWithMode(const QString& remote, bool force);
};
} // namespace litecode::workspace
Q_DECLARE_METATYPE(litecode::workspace::GitComparison)
Q_DECLARE_METATYPE(litecode::workspace::GitBranch)
Q_DECLARE_METATYPE(litecode::workspace::GitRemote)
Q_DECLARE_METATYPE(litecode::workspace::GitCommit)
Q_DECLARE_METATYPE(litecode::workspace::GitCommitFile)
Q_DECLARE_METATYPE(litecode::workspace::GitDivergence)
