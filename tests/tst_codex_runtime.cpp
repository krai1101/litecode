#include "codex/AppServer.h"
#include "codex/ChatSession.h"
#include "codex/TurnUndo.h"
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace litecode;
class CodexRuntimeTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void fragmentedProtocolAndServerRequests() {
        codex::AppServer server;
        QSignalSpy notifications(&server, &codex::AppServer::notification);
        QSignalSpy interactions(&server, &codex::AppServer::interaction);
        server.ingest("{\"method\":\"sample\",\"params\":{\"text\":\"hel");
        QCOMPARE(notifications.size(), 0);
        server.ingest("lo\"}}\n{\"id\":\"request-1\",\"method\":\"approval\",\"params\":{}}\n");
        QCOMPARE(notifications.size(), 1);
        QCOMPARE(interactions.size(), 1);
        QCOMPARE(interactions.first().first().value<QJsonValue>().toString(), QString("request-1"));
    }

    void requestStateAndFailureIsolation() {
        QTemporaryDir project, storage;
        codex::ChatSession session(nullptr, storage.path());
        session.setWorkspace(project.path());
        session.connectRuntime(LITECODE_CODEX_FIXTURE);
        QTRY_VERIFY(session.server().ready());
        QTRY_VERIFY(!session.models().isEmpty());
        QSignalSpy accepted(&session, &codex::ChatSession::submissionAccepted);
        session.submit(QJsonArray{QJsonObject{{"type", "text"}, {"text", "fixture wait"}}},
                       {}, {}, false);
        QTRY_COMPARE(accepted.size(), 1);
        QTRY_COMPARE(session.turns().size(), 1);
        QTRY_COMPARE(session.turns().first().items.size(), 1);
        const auto items = session.turns().first().items;
        const auto turnId = session.activeTurn();
        QVERIFY(!turnId.isEmpty());
        session.consume("item/agentMessage/delta", {{"threadId", "another-thread"},
                                                    {"turnId", turnId},
                                                    {"itemId", "unexpected-item"},
                                                    {"delta", "Wrong thread"}});
        QCOMPARE(session.turns().first().items, items);

        QSignalSpy errors(&session, &codex::ChatSession::error);
        session.submit(
            QJsonArray{QJsonObject{{"type", "text"}, {"text", "fixture rejected follow-up"}}},
            {}, {}, false);
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(session.activeTurn(), turnId);
        QCOMPARE(accepted.size(), 1);
        session.interrupt();
        QTRY_VERIFY(!session.busy());
        QCOMPARE(session.turns().first().status, QString("interrupted"));
    }

    void safeTurnUndo() {
        QTemporaryDir project;
        const auto path = project.filePath("one.cpp");
        auto put = [](const QString& path, const QByteArray& bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(put(path, "keep\nnew\n"));
        const QString diff = "--- a/one.cpp\n+++ b/one.cpp\n@@ -1,2 +1,2 @@\n keep\n-old\n+new\n";
        codex::TurnUndo undo(diff, project.path());
        QVERIFY2(undo.available(), qPrintable(undo.reason()));
        QString error;
        QVERIFY(undo.apply(&error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("keep\nold\n"));
        file.close();
        QVERIFY(!undo.available());
        QVERIFY(put(path, "keep\nnew\n"));
        codex::TurnUndo changed(diff, project.path());
        QVERIFY(put(path, "user edits\n"));
        QVERIFY(!changed.apply(&error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("user edits\n"));
        file.close();
        codex::TurnUndo outside(
            "--- a/../outside.cpp\n+++ b/../outside.cpp\n@@ -1 +1 @@\n-a\n+b\n", project.path());
        QVERIFY(!outside.available());
        codex::TurnUndo deleted("--- a/deleted.cpp\n+++ /dev/null\n@@ -1 +0,0 @@\n-old\n",
                                   project.path());
        QVERIFY(deleted.available());
        QVERIFY(deleted.apply(&error));
        QFile restored(project.filePath("deleted.cpp"));
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), QByteArray("old\n"));
    }

    void bundledRuntimeSmoke() {
        const auto executable = qEnvironmentVariable("LITECODE_CODEX_SMOKE");
        if (executable.isEmpty())
            QSKIP("Set LITECODE_CODEX_SMOKE to exercise the pinned native runtime.");
        codex::AppServer server;
        server.start(executable);
        QTRY_VERIFY_WITH_TIMEOUT(server.ready(), 20000);
        bool complete = false;
        QString failure;
        QJsonObject result;
        server.request("model/list", {}, [&](const QJsonObject& r, const QString& e) {
            result = r;
            failure = e;
            complete = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(complete, 20000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(!result.value("data").toArray().isEmpty());
        QStringList modelIds;
        for (const auto& model : result.value("data").toArray())
            modelIds.append(model.toObject().value("model").toString());
        qInfo().noquote() << "Runtime catalog:" << modelIds.join(", ");
        QTemporaryDir project;
        complete = false;
#ifdef Q_OS_WIN
        QJsonArray command{"cmd.exe", "/d", "/c", "echo litecode-runtime-ok"};
#else
        QJsonArray command{"/bin/sh", "-c", "printf litecode-runtime-ok"};
#endif
        server.request("command/exec",
                       {{"command", command},
                        {"cwd", project.path()},
                        {"sandboxPolicy", QJsonObject{{"type", "readOnly"}}}},
                       [&](const QJsonObject& r, const QString& e) {
                           result = r;
                           failure = e;
                           complete = true;
                       });
        QTRY_VERIFY_WITH_TIMEOUT(complete, 30000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(result.value("exitCode").toInt(-1), 0);
        QVERIFY(result.value("stdout").toString().contains("litecode-runtime-ok"));
        complete = false;
        server.request("thread/start",
                       {{"cwd", project.path()},
                        {"sandbox", "workspace-write"},
                        {"approvalPolicy", "on-request"}},
                       [&](const QJsonObject& r, const QString& e) {
                           result = r;
                           failure = e;
                           complete = true;
                       });
        QTRY_VERIFY_WITH_TIMEOUT(complete, 20000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(!result.value("thread").toObject().value("id").toString().isEmpty());
        complete = false;
        server.request("thread/list",
                       {{"cwd", project.path()},
                        {"sortKey", "updated_at"},
                        {"sourceKinds", QJsonArray{"appServer", "cli", "vscode"}}},
                       [&](const QJsonObject& r, const QString& e) {
                           result = r;
                           failure = e;
                           complete = true;
                       });
        QTRY_VERIFY_WITH_TIMEOUT(complete, 20000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        server.stop();
        QTest::qWait(100);
    }
    void realAgentCreatesFile() {
        const auto executable = qEnvironmentVariable("LITECODE_CODEX_AGENT_SMOKE");
        if (executable.isEmpty())
            QSKIP("Optional real authenticated tool round-trip.");
        QTemporaryDir project;
        codex::ChatSession session;
        session.setWorkspace(project.path());
        session.connectRuntime(executable);
        QTRY_VERIFY_WITH_TIMEOUT(session.server().ready(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.models().isEmpty(), 20000);
        QString model;
        for (const auto& value : session.models())
            if (value.toObject().value("isDefault").toBool())
                model = value.toObject().value("model").toString();
        QString failure;
        connect(&session, &codex::ChatSession::error, &session,
                [&](const QString& e) { failure = e; });
        connect(&session, &codex::ChatSession::interaction, &session,
                [&](const QJsonValue& id, const QString&, const QJsonObject&) {
                    session.server().respond(id, {{"decision", "decline"}});
                });
        QJsonArray inputs{QJsonObject{
            {"type", "text"},
            {"text",
             "Integration test in this temporary workspace only: create smoke.txt containing "
             "exactly litecode-agent-ok. Use the file-editing tool, then run a shell command "
             "to read that file and verify it. Do not read unrelated files or access the "
             "network. Final answer: Verified. If a tool fails report the failure."},
            {"text_elements", QJsonArray{}}}};
        for (const auto& value : session.models()) {
            const auto option = value.toObject();
            if (option.value("model") != model ||
                !option.value("inputModalities").toArray().contains("image"))
                continue;
            QImage image(16, 16, QImage::Format_RGB32);
            image.fill(Qt::blue);
            const auto imagePath = project.filePath("attachment.png");
            QVERIFY(image.save(imagePath));
            inputs.append(QJsonObject{{"type", "localImage"}, {"path", imagePath}});
            break;
        }
        session.submit(inputs, model, {}, false);
        QTRY_VERIFY_WITH_TIMEOUT(!session.turns().isEmpty() || !failure.isEmpty(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 150000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QFile file(project.filePath("smoke.txt"));
        QString diagnostic;
        for (const auto& turn : session.turns())
            for (const auto& item : turn.items)
                if (item.value("type") == "agentMessage")
                    diagnostic += item.value("text").toString();
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(diagnostic));
        QCOMPARE(file.readAll().trimmed(), QByteArray("litecode-agent-ok"));
        file.close();
        bool command = false, final = false;
        for (const auto& turn : session.turns())
            for (const auto& item : turn.items) {
                command |= item.value("type") == "commandExecution";
                final |=
                    item.value("type") == "agentMessage" && item.value("phase") == "final_answer";
            }
        QVERIFY(command);
        QVERIFY(final);
        const auto thread = session.threadId();
        session.rename("LiteCode integration test");
        QTRY_COMPARE(session.title(), QString("LiteCode integration test"));
        session.resume(thread);
        QTRY_VERIFY(!session.busy());
        QVERIFY(!session.turns().isEmpty());
        bool goalDone = false;
        QString goalFailure;
        session.server().request(
            "thread/goal/set",
            {{"threadId", thread}, {"objective", "Integration test goal"}, {"status", "paused"}},
            [&](const QJsonObject& result, const QString& error) {
                goalFailure = error;
                goalDone = result.value("goal").toObject().value("status") == "paused";
            });
        QTRY_VERIFY_WITH_TIMEOUT(goalDone || !goalFailure.isEmpty(), 10000);
        QVERIFY2(goalFailure.isEmpty(), qPrintable(goalFailure));
        QVERIFY(goalDone);
        QTRY_VERIFY(!session.goal().isEmpty());
        session.clearGoal();
        QTRY_VERIFY(session.goal().isEmpty());
        session.archive();
        QTRY_VERIFY(session.threadId().isEmpty());
        session.server().stop();
        QTest::qWait(200);
    }
};

QTEST_GUILESS_MAIN(CodexRuntimeTests)
#include "tst_codex_runtime.moc"
