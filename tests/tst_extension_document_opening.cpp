#include "extensions/HostDocumentOpening.h"
#include "extensions/WorkspaceEditTransfer.h"
#include <QtTest>
#include <stdexcept>
using namespace litecode::extensions;
class DocumentOpeningTests final : public QObject {
    Q_OBJECT
  private slots:
    void largeWorkspaceEditSendFailureAndShutdownRejectPendingFutures() {
        WorkspaceEdit edit;
        edit.insert(QUrl("untitled:large"), Position(0, 0), QString(9 * 1024 * 1024, QLatin1Char('x')));
        const WorkspaceEditRequest request{detail::WorkspaceEditAccess::entries(edit), {}, {}};
        int sent = 0;
        HostWorkspaceRequests failing([&](const QJsonObject&) {
            if (++sent == 2) throw std::runtime_error("Transport failed.");
        });
        auto failed = failing.applyEdit(request);
        QTRY_VERIFY(failed.isFinished());
        QCOMPARE(sent, 2);
        QVERIFY_EXCEPTION_THROWN(failed.result(), std::exception);
        int stoppedSends = 0;
        HostWorkspaceRequests stopped([&](const QJsonObject&) { ++stoppedSends; });
        auto cancelled = stopped.applyEdit(request);
        stopped.shutdown();
        QCoreApplication::processEvents();
        QTRY_VERIFY(cancelled.isFinished());
        QCOMPARE(stoppedSends, 0);
        QVERIFY_EXCEPTION_THROWN(cancelled.result(), std::exception);
    }
    void largeWorkspaceEditRpcRetainsItsFutureAndRequestIdentity() {
        QVector<QJsonObject> sent;
        HostWorkspaceRequests requests([&](const auto& message) { sent.append(message); });
        WorkspaceEdit edit;
        edit.insert(QUrl("untitled:large"), Position(0, 0), QString(9 * 1024 * 1024, QLatin1Char('x')));
        auto future = requests.applyEdit({detail::WorkspaceEditAccess::entries(edit), {}, {}});
        QTRY_VERIFY(!sent.isEmpty());
        QCOMPARE(sent.first().value("event").toString(), QString("workspaceEditBegin"));
        QVERIFY(!future.isFinished());
        wire::WorkspaceEditTransferDecoder decoder;
        std::optional<QJsonObject> message;
        QString error;
        for (const auto& frame : sent) {
            if (auto result = decoder.accept(frame, &error)) message = std::move(result);
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
        QVERIFY(message);
        QCOMPARE(message->value("requestId").toString(), QString("1"));
        QVERIFY(wire::decodeWorkspaceEdit(*message));
        requests.complete(1, {{"applied", true}}, {});
        QTRY_VERIFY(future.isFinished());
        QVERIFY(future.result());
    }
    void eolOnlySnapshotsEmitContentChangeEvenWithoutLineBreaks() {
        HostDocuments documents({});
        documents.accept({"1", QUrl("untitled:test"), "plaintext", "utf8", "text", 1, true, EndOfLine::LF});
        int changes = 0;
        auto subscription = documents.onDidChangeTextDocument()([&](const TextDocumentChangeEvent& event) {
            ++changes;
            QCOMPARE(event.contentChanges.size(), 1);
            QCOMPARE(event.contentChanges.first().text, QString("text"));
            QCOMPARE(event.reason, std::optional(TextDocumentChangeReason::Undo));
            QCOMPARE(event.document->eol(), EndOfLine::CRLF);
        });
        TextDocumentSnapshot snapshot{"1", QUrl("untitled:test"), "plaintext", "utf8", "text", 2, true, EndOfLine::CRLF, TextDocumentChangeReason::Undo};
        QVector<QJsonObject> frames;
        wire::sendDocumentSnapshot(snapshot, [&](const auto& frame) { frames.append(frame); });
        wire::DocumentSnapshotDecoder decoder;
        for (const auto& frame : frames) {
            QString error;
            if (auto decoded = decoder.accept(frame, &error)) documents.accept(*decoded);
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
        QCOMPARE(changes, 1);
        subscription.dispose();
    }
    void batchDecoderHandlesChunksAndRejectsIncompleteTransactions() {
        const QString large(70000, QLatin1Char('x'));
        const wire::DocumentBatch batch{"1", 1, 2,
            {TextEdit(Range(0, 0, 0, 0), large), TextEdit(Range(0, 0, 0, 0), "tail")}, true, {}};
        QVector<QJsonObject> frames;
        wire::sendDocumentBatch(batch, [&](const QJsonObject& frame) { frames.append(frame); });
        wire::DocumentBatchDecoder decoder;
        std::optional<wire::DocumentBatch> result;
        for (const auto& frame : frames) {
            QString error;
            if (auto completed = decoder.accept(frame, &error)) result = std::move(completed);
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
        QVERIFY(result);
        QCOMPARE(result->edits.size(), 2);
        QCOMPARE(result->edits.first().newText, large);
        QString error;
        QVERIFY(!decoder.accept(frames.first(), &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!decoder.accept(frames.last(), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!decoder.transferring());
        auto badHeader = frames.first();
        badHeader.insert("count", "100001");
        QVERIFY(!decoder.accept(badHeader, &error));
        QVERIFY(!error.isEmpty());
    }
    void batchedWireChangeCommitsOneVersionAndOneEvent() {
        HostDocuments documents({});
        documents.accept({"1", QUrl("generated:/test"), "plaintext", "utf8", "first\nstable\nlast", 1, false, EndOfLine::LF});
        const auto document = documents.find("1");
        int eventCount = 0;
        auto listener = documents.onDidChangeTextDocument()([&](const TextDocumentChangeEvent& event) {
            ++eventCount;
            QCOMPARE(event.document, document);
            QCOMPARE(event.contentChanges.size(), 2);
            QCOMPARE(document->getText(), QString("FIRST\nstable\nLAST"));
            QCOMPARE(document->version(), qint64(2));
        });
        const wire::DocumentBatch batch{"1", 1, 2,
            {TextEdit(Range(2, 0, 2, 4), "LAST"), TextEdit(Range(0, 0, 0, 5), "FIRST")}, false, {}};
        QVector<QJsonObject> frames;
        wire::sendDocumentBatch(batch, [&](const QJsonObject& frame) { frames.append(frame); });
        wire::DocumentBatchDecoder decoder;
        for (qsizetype index = 0; index < frames.size(); ++index) {
            QString error;
            auto decoded = decoder.accept(frames[index], &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            if (decoded) documents.changeBatch(*decoded);
            if (index + 1 < frames.size()) {
                QCOMPARE(document->getText(), QString("first\nstable\nlast"));
                QCOMPARE(eventCount, 0);
            }
        }
        QCOMPARE(eventCount, 1);
        const wire::DocumentBatch invalid{"1", 2, 3,
            {TextEdit(Range(2, 0, 2, 4), "broken"), TextEdit(Range(2, 1, 2, 3), "overlap")}, false, {}};
        QVERIFY_EXCEPTION_THROWN(documents.changeBatch(invalid), std::invalid_argument);
        QCOMPARE(document->getText(), QString("FIRST\nstable\nLAST"));
        QCOMPARE(document->version(), qint64(2));
        QCOMPARE(eventCount, 1);
        listener.dispose();
    }
    void simultaneousLoadsShareRpcAndCachedObject() {
        QVector<QJsonObject> sent;
        HostWorkspaceRequests requests([&sent](const QJsonObject& message) { sent.append(message); });
        HostDocuments documents([](const QString&) { QPromise<bool> promise; promise.start(); promise.addResult(false); promise.finish(); return promise.future(); });
        HostDocumentOpening opening(documents, requests);
        const QUrl uri("file:///fixture.py");
        auto first = opening.open(uri, {});
        auto second = opening.open(uri, {});
        QTRY_COMPARE(sent.size(), 1);
        documents.accept({"1", uri, "python", "utf8", "text", 1, false, EndOfLine::LF});
        requests.complete(sent.first().value("requestId").toString().toULongLong(), {{"documentId", "1"}}, {});
        QTRY_VERIFY(first.isFinished());
        QTRY_VERIFY(second.isFinished());
        QCOMPARE(first.result(), second.result());
        QCOMPARE(first.result(), documents.find("1"));
        auto cached = opening.open(uri, {});
        QTRY_VERIFY(cached.isFinished());
        QCOMPARE(cached.result(), first.result());
        QCOMPARE(sent.size(), 1);
    }
    void differentEncodingJoinsThenReloadsWithoutReplacingObject() {
        QVector<QJsonObject> sent;
        HostWorkspaceRequests requests([&sent](const QJsonObject& message) { sent.append(message); });
        HostDocuments documents({});
        HostDocumentOpening opening(documents, requests);
        const QUrl uri("file:///fixture.txt");
        auto original = opening.open(uri, {});
        auto redecoded = opening.open(uri, {QString("utf16le")});
        QTRY_COMPARE(sent.size(), 1);
        documents.accept({"1", uri, "plaintext", "utf8", "original", 1, false, EndOfLine::LF});
        requests.complete(sent.first().value("requestId").toString().toULongLong(), {{"documentId", "1"}}, {});
        QTRY_VERIFY(original.isFinished());
        const auto identity = original.result();
        QTRY_COMPARE(sent.size(), 2);
        QCOMPARE(sent.last().value("encoding").toString(), QString("utf16le"));
        documents.accept({"1", uri, "plaintext", "utf16le", "decoded", 2, false, EndOfLine::LF});
        requests.complete(sent.last().value("requestId").toString().toULongLong(), {{"documentId", "1"}}, {});
        QTRY_VERIFY(redecoded.isFinished());
        QCOMPARE(redecoded.result(), identity);
        QCOMPARE(identity->getText(), QString("decoded"));
    }
    void shutdownRejectsCallersBeforeDispatcherRuns() {
        HostWorkspaceRequests requests([](const QJsonObject&) {});
        HostDocuments documents({});
        HostDocumentOpening opening(documents, requests);
        auto first = opening.open(QUrl("file:///pending"), {});
        auto created = opening.create({});
        opening.shutdown();
        QVERIFY(first.isFinished());
        QVERIFY(created.isFinished());
        QVERIFY_EXCEPTION_THROWN(first.result(), std::exception);
        QVERIFY_EXCEPTION_THROWN(created.result(), std::exception);
    }
};
QTEST_GUILESS_MAIN(DocumentOpeningTests)
#include "tst_extension_document_opening.moc"
