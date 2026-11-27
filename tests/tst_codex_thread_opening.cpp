#include "codex/ChatSession.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace litecode::codex;
class CodexThreadOpeningTests final : public QObject {
    Q_OBJECT
  private slots:
    void chatListCompletesWhileOpeningAConversation() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.accountKnown());

        QSignalSpy history(&session, &ChatSession::historyReady);
        QSignalSpy opened(&session, &ChatSession::threadOpened);
        session.history();
        session.resume("thread-fixture");

        QTRY_COMPARE(opened.size(), 1);
        QTRY_COMPARE(history.size(), 1);
        QVERIFY(!history.first().at(0).toJsonArray().isEmpty());
    }
    void authenticationUsesServerAccountAndKeepsCancellationSeparate() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.accountKnown());
        QVERIFY(!session.account().isEmpty());
        QSignalSpy urls(&session, &ChatSession::loginUrl);
        QSignalSpy errors(&session, &ChatSession::error);
        session.signOut();
        QTRY_VERIFY(!session.authBusy());
        QVERIFY(session.account().isEmpty());
        QVERIFY(session.requiresAuth());
        session.submit(QJsonArray{QJsonObject{{"type", "text"}, {"text", "must not run"}}},
                       "fixture", "medium", false);
        QVERIFY(!errors.isEmpty());
        QVERIFY(!session.busy());
        session.signIn();
        session.cancelSignIn(); // Cancel before login/start supplies its ID.
        QTest::qWait(100);
        QCOMPARE(urls.size(), 0);
        QVERIFY(!session.signingIn());
        session.signIn();
        QTRY_COMPARE(urls.size(), 1);
        QVERIFY(session.signingIn());
        session.cancelSignIn();
        QVERIFY(!session.signingIn());
        // Completion of the cancelled browser login must not finish a later login.
        session.signInWithDeviceCode();
        QTRY_COMPARE(session.deviceCode(), QString("FIXTURE-CODE"));
        session.consume("account/login/completed", {{"loginId", "fixture-browser-login"},
                                                    {"success", true}});
        QVERIFY(session.signingIn());
        session.cancelSignIn();
        QVERIFY(session.deviceCode().isEmpty());
        session.signInWithApiKey("wrong-fixture-key");
        QTRY_VERIFY(!session.authBusy());
        QVERIFY(session.account().isEmpty());
        session.signInWithApiKey("fixture-api-key");
        QTRY_COMPARE(session.account().value("type").toString(), QString("apiKey"));
        QVERIFY(!session.signingIn());
        QVERIFY(!session.authBusy());
    }
    void terminalErrorsStayWithTheirTurn() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        QSignalSpy opened(&session, &ChatSession::threadOpened);
        session.resume("thread-fixture");
        QTRY_COMPARE(opened.size(), 1);
        const QString message = "You've hit your usage limit.";
        const QString id = "limited-turn";
        session.consume("turn/started", {{"threadId", session.threadId()},
            {"turn", QJsonObject{{"id", id}, {"status", "inProgress"}}}});
        session.consume("error", {{"threadId", session.threadId()}, {"turnId", id},
                                  {"willRetry", true},
                                  {"error", QJsonObject{{"message", "Retrying connection"}}}});
        QVERIFY(session.turns().last().error.isEmpty());
        session.consume("error", {{"threadId", session.threadId()}, {"turnId", id},
                                  {"willRetry", false},
                                  {"error", QJsonObject{{"message", message}}}});
        QCOMPARE(session.turns().last().error, message);
        session.consume("turn/completed", {{"threadId", session.threadId()},
            {"turn", QJsonObject{{"id", id}, {"status", "failed"}}}});
        QCOMPARE(session.turns().last().error, message);
        session.consume("turn/completed", {{"threadId", session.threadId()},
            {"turn", QJsonObject{{"id", id}, {"status", "completed"}}}});
        QVERIFY(session.turns().last().error.isEmpty());
    }
    void readOnlyHistoryFollowsTheOwningSessionsCompletion() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.setViewVisible(false);
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        QSignalSpy opened(&session, &ChatSession::threadOpened);
        QSignalSpy resets(&session, &ChatSession::reset);
        session.resume("thread-observed");
        QTRY_COMPARE(opened.size(), 1);
        QVERIFY(session.readOnly());
        QCOMPARE(session.turns().last().status, QString("inProgress"));
        QCOMPARE(session.turns().last().durationMs, qint64(-1));
        session.refreshObservedThread(); // Hidden views do not fetch a new snapshot.
        QTest::qWait(50);
        QCOMPARE(session.turns().last().items.size(), 1);
        session.setViewVisible(true);
        QTRY_COMPARE_WITH_TIMEOUT(session.turns().last().status, QString("completed"), 6000);
        QCOMPARE(session.turns().last().items.last().value("text").toString(),
                 QString("The answer completed in the owning session."));
        QCOMPARE(session.turns().last().durationMs, qint64(65000));
        QCOMPARE(opened.size(), 1);
        QCOMPARE(resets.size(), 1); // Updating a read-only snapshot must not reopen the chat.
        QVERIFY(!session.opening());
        QVERIFY(!session.busy());
        QVERIFY(!session.canWrite());
        session.newChat();
        session.refreshObservedThread();
        QTest::qWait(50);
        QVERIFY(session.turns().isEmpty());
    }
    void anotherWriterOpensReadOnlyAndCanRetry() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        QSignalSpy opened(&session, &ChatSession::threadOpened);
        QSignalSpy failed(&session, &ChatSession::threadOpenFailed);
        QSignalSpy errors(&session, &ChatSession::error);
        session.resume("thread-owned");
        QTRY_COMPARE(opened.size(), 1);
        QCOMPARE(failed.size(), 0);
        QVERIFY(session.readOnly());
        QVERIFY(!session.canWrite());
        QVERIFY(!session.busy()); // The owner's active turn is not ours.
        QVERIFY(session.activeTurn().isEmpty());
        QCOMPARE(session.threadId(), "thread-owned");
        QVERIFY(!session.turns().isEmpty());
        session.submit(QJsonArray{QJsonObject{{"type", "text"}, {"text", "must not submit"}}},
                       "fixture", "medium", false);
        session.rename("must not rename");
        session.archive();
        session.compact();
        session.updateGoal("active");
        session.clearGoal();
        session.editAndSubmit("saved-turn", "saved-answer", "must not edit", "fixture", "medium");
        QCOMPARE(errors.size(), 7);
        QCOMPARE(session.threadId(), "thread-owned");
        session.resume("thread-owned"); // Fixture releases the writer on retry.
        QTRY_COMPARE(opened.size(), 2);
        QVERIFY(!session.readOnly());
        QVERIFY(session.canWrite());
        QVERIFY(!session.opening());
    }
    void cancelOpeningAllowsNavigationAndIgnoresLateReply() {
        QTemporaryDir storage;
        ChatSession session(nullptr, storage.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        QSignalSpy opened(&session, &ChatSession::threadOpened);
        QSignalSpy failed(&session, &ChatSession::threadOpenFailed);
        session.resume("thread-slow-open");
        QVERIFY(session.opening());
        session.cancelOpen();
        QVERIFY(!session.opening());
        QVERIFY(!session.busy());
        session.newChat();
        QTest::qWait(1000);
        QCOMPARE(opened.size(), 0);
        QCOMPARE(failed.size(), 0);
        QVERIFY(session.threadId().isEmpty());
        session.resume("thread-fixture");
        QTRY_COMPARE(opened.size(), 1);
    }
    void fastTierIsSentAndExplicitlyClearedForStandardSpeed() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        const auto accepted = [&] {
            for (const auto& turn : session.turns())
                for (const auto& item : turn.items)
                    if (item.value("type") == "agentMessage" &&
                        item.value("text") == "tier-accepted")
                        return true;
            return false;
        };
        session.setServiceTier("priority");
        session.submit({QJsonObject{{"type", "text"}, {"text", "fast-tier-required"}}}, "fixture",
                       "medium", false);
        QTRY_VERIFY(!session.busy());
        QTRY_VERIFY(accepted());
        session.newChat();
        session.setServiceTier({});
        session.submit({QJsonObject{{"type", "text"}, {"text", "standard-tier-required"}}},
                       "fixture", "medium", false);
        QTRY_VERIFY(!session.busy());
        QTRY_VERIFY(accepted());
    }
    void collaborationAndPermissionsApplyToEachTurn() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        const QVector<ApprovalMode> modes{ApprovalMode::Ask, ApprovalMode::AutoReview,
                                          ApprovalMode::FullAccess};
        const QStringList labels{"ask", "auto", "full"};
        for (int i = 0; i < modes.size(); ++i) {
            session.setApprovalMode(modes[i]);
            // Stay in the same thread: turning Plan off must explicitly clear it.
            for (bool plan : {true, false}) {
                session.setPlanMode(plan);
                QSignalSpy accepted(&session, &ChatSession::submissionAccepted);
                session.submit({QJsonObject{{"type", "text"},
                                            {"text", "policy-check:" + labels[i] +
                                                         (plan ? ":plan" : ":default")}}},
                               "fixture", "medium", false);
                QTRY_COMPARE(accepted.size(), 1);
                QTRY_VERIFY(!session.busy());
                QVERIFY(!session.turns().isEmpty());
                const auto items = session.turns().last().items;
                bool correct = false;
                for (const auto& item : items)
                    if (item.value("type") == "agentMessage")
                        correct = item.value("text") == "policy-accepted";
                QVERIFY(correct);
            }
        }
    }
    void failedOpenPreservesCurrentChatAndDraftOwnership() {
        QTemporaryDir storage, workspace;
        ChatSession session(nullptr, storage.path());
        session.setWorkspace(workspace.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        QSignalSpy opened(&session, &ChatSession::threadOpened);
        QSignalSpy failed(&session, &ChatSession::threadOpenFailed);
        session.resume("thread-fixture");
        QTRY_COMPARE(opened.size(), 1);
        session.resume("thread-missing");
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(session.threadId(), "thread-fixture");
        QCOMPARE(session.title(), "Fixture chat");
        QVERIFY(!session.opening());
        QVERIFY(!session.busy());
        QVERIFY(session.canWrite());
        session.newChat();
        QVERIFY(session.threadId().isEmpty());
    }
};
QTEST_GUILESS_MAIN(CodexThreadOpeningTests)
#include "tst_codex_thread_opening.moc"
