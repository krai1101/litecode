#pragma once
#include "CodexExecutionPolicy.h"
#include "ObservedTurnLifecycle.h"
#include "codex/AppServer.h"
#include <QJsonArray>
#include <QTimer>
#include <QThreadPool>
#include <QSet>
#include <QVector>
#include <memory>

namespace litecode::codex {
struct TurnState {
    QString id, status;
    QVector<QJsonObject> items;
    qint64 startedMs{}, durationMs{-1};
    QString diff, error;
    bool sentAsGoal{false};
    QSet<QString> activeTools;
};
class ChatSession final : public QObject {
    Q_OBJECT
  public:
    explicit ChatSession(QObject* parent = nullptr, QString storageDirectory = {});
    ~ChatSession() override;
    AppServer& server() { return server_; }
    void connectRuntime(const QString& executable = {});
    void setApprovalMode(ApprovalMode mode) { approvalMode_ = mode; }
    void setPlanMode(bool enabled) { planMode_ = enabled; }
    void setServiceTier(const QString& tier) { serviceTier_ = tier; }
    void setWorkspace(const QString& path);
    void newChat();
    void resume(const QString& id);
    void cancelOpen();
    void history(const QString& cursor = {});
    void loadOlderTurns();
    bool hasOlderTurns() const { return !turnsCursor_.isEmpty(); }
    void rename(const QString& name);
    void archive();
    void submit(const QJsonArray& input, const QString& model, const QString& effort, bool goal);
    void editAndSubmit(const QString& turnId, const QString& itemId, const QString& text,
                       const QString& model, const QString& effort);
    void interrupt();
    void compact();
    void updateGoal(const QString& status);
    void clearGoal();
    void refreshModels();
    void setViewVisible(bool visible);
    void refreshObservedThread();
    bool hasRunningTurn() const;
    void refreshAccount();
    void signIn();
    void signInWithApiKey(QString key);
    void signInWithDeviceCode();
    void cancelSignIn();
    bool signingIn() const { return loginPending_ || !loginId_.isEmpty(); }
    bool authBusy() const { return loginPending_ || logoutPending_; }
    bool accountKnown() const { return accountKnown_; }
    bool requiresAuth() const { return requiresAuth_; }
    QString deviceCode() const { return deviceCode_; }
    void signOut();
    QString workspace() const { return workspace_; }
    QString threadWorkspace() const { return threadWorkspace_.isEmpty() ? workspace_ : threadWorkspace_; }
    QString threadId() const { return threadId_; }
    QString title() const { return title_; }
    QString activeTurn() const { return activeTurn_; }
    bool busy() const { return submitting_ || !activeTurn_.isEmpty(); }
    bool opening() const { return opening_; }
    bool readOnly() const { return readOnly_; }
    bool canWrite() const { return !readOnly_ && (threadId_.isEmpty() || threadReady_); }
    const QVector<TurnState>& turns() const { return turns_; }
    QJsonArray models() const { return models_; }
    QJsonObject account() const { return account_; }
    QJsonObject limits() const { return limits_; }
    QJsonObject usage() const { return usage_; }
    QJsonObject goal() const { return goal_; }
    void consume(const QString& method, const QJsonObject& params);
  signals:
    void reset();
    void changed(const QString& turnId);
    void stateChanged();
    void catalogChanged();
    void modelSelectionChanged(const QString& model, const QString& effort);
    void accountChanged();
    void historyReady(const QJsonArray& threads, const QString& nextCursor);
    void historyFailed(const QString& message);
    void threadOpened();
    void threadOpenFailed(const QString& message);
    void olderTurnsLoaded();
    void submissionAccepted();
    void error(const QString& message);
    void errorCleared();
    void loginUrl(const QString& url);
    void interaction(const QJsonValue& id, const QString& method, const QJsonObject& params);
    void interactionResolved(const QJsonValue& id);
    void fileChangesProposed(const QString& threadId, const QString& turnId,
                             const QJsonArray& changes);
    void revisionPrepared(const QJsonArray& input, int textIndex);

  private:
    using Completion = std::function<void()>;
    void updateObservation();
    void readObservedPage(const QJsonObject& page, const QString& path, quint64 epoch,
                          std::function<void(QJsonObject)> done);
    void ensureThread(const QString& model, Completion done);
    void loadThread(const QJsonObject& thread);
    void readOwnedThread(const QString& id, quint64 epoch);
    void finishOpen(QJsonObject thread, const QJsonObject& page, bool readOnly, quint64 epoch);
    void failOpen(const QString& message, quint64 epoch);
    bool requireWritable();
    void catalog(const QString& cursor, quint64 generation, QJsonArray collected);
    TurnState& ensureTurn(const QString& id);
    void putItem(TurnState& turn, const QJsonObject& item);
    void persistThread();
    void startLogin(QJsonObject params);
    void authNotification(const QString& method, const QJsonObject& params);
    AppServer server_;
    QString workspace_, threadWorkspace_, threadId_, title_, activeTurn_, loginId_;
    QString storageDirectory_;
    QString serviceTier_;
    ApprovalMode approvalMode_{ApprovalMode::Ask};
    bool planMode_{};
    QString turnsCursor_;
    QVector<TurnState> turns_;
    QJsonArray models_;
    QJsonObject account_, limits_, usage_, goal_;
    QHash<QString, QString> reportedTurnErrors_;
    quint64 epoch_{0};
    quint64 catalogGeneration_{0};
    quint64 historyGeneration_{0};
    quint64 authGeneration_{}, accountGeneration_{};
    bool accountKnown_{}, requiresAuth_{true}, loginPending_{}, logoutPending_{};
    QString deviceCode_;
    QTimer observationTimer_;
    QJsonObject observedTail_;
    QString observationError_;
    QString observedRolloutPath_;
    ObservedTurnLifecycle observedLifecycle_;
    QThreadPool observationPool_;
    std::shared_ptr<std::atomic_bool> observationCancelled_{std::make_shared<std::atomic_bool>(false)};
    bool viewVisible_{true}, observationInFlight_{};
    bool submitting_{false};
    bool opening_{false}, readOnly_{false}, threadReady_{false};
};
} // namespace litecode::codex
