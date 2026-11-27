#include "extensions/HostClipboard.h"
#include "extensions/WorkbenchClipboard.h"
#include "extensions/JsonMessageTransfer.h"
#include "extensions/ExtensionWireProtocol.h"
#include <QJsonDocument>
#include <QtTest>
#include <stdexcept>

using namespace litecode::extensions;
class ExtensionClipboardTests final : public QObject {
    Q_OBJECT
  private slots:
    void largeClipboardReadAndWriteUseCompleteTransfers() {
        QString clipboard;
        int writes = 0, requestChunks = 0, responseChunks = 0;
        WorkbenchClipboard workbench;
        workbench.setBackend([&] { return clipboard; }, [&](const QString& value) { ++writes; clipboard = value; });
        wire::JsonMessageTransferDecoder requests({"event", "clipboard", "clipboard"});
        wire::JsonMessageTransferDecoder responses({"command", "clipboardResult", "clipboardResult"});
        HostClipboard* host = nullptr;
        HostClipboard service([&](const QJsonObject& frame) {
            auto request = frame;
            QString error;
            if (requests.recognizes(frame) || requests.transferring()) {
                if (frame.value("event") == QJsonValue("clipboardChunk")) ++requestChunks;
                auto completed = requests.accept(frame, &error);
                QVERIFY2(error.isEmpty(), qPrintable(error));
                if (!completed) { QCOMPARE(writes, 0); return; }
                request = *completed;
            }
            const auto id = request.value("requestId").toString();
            QVERIFY(workbench.accept(request, [&](const QJsonValue& result, const QString& backendError) {
                wire::sendJsonTransfer({{"command", "clipboardResult"}, {"requestId", id}, {"result", result}, {"error", backendError}},
                    {"command", "clipboardResult", "clipboardResult"}, [&](const QJsonObject& responseFrame) {
                        QVERIFY(!wire::encode(responseFrame).isEmpty());
                        auto response = responseFrame;
                        if (responses.recognizes(responseFrame) || responses.transferring()) {
                            if (responseFrame.value("command") == QJsonValue("clipboardResultChunk")) ++responseChunks;
                            auto completed = responses.accept(responseFrame, &error);
                            QVERIFY2(error.isEmpty(), qPrintable(error));
                            if (!completed) return;
                            response = *completed;
                        }
                        host->complete(id.toULongLong(), response.value("result"), response.value("error").toString());
                    });
            }));
        });
        host = &service;
        const auto text = QString(9 * 1024 * 1024, QLatin1Char('x')) + QString::fromUtf8("\n\xf0\x9f\x98\x80");
        auto write = service.writeText(text);
        QTRY_VERIFY(write.isFinished());
        write.waitForFinished();
        QCOMPARE(writes, 1);
        QCOMPARE(clipboard, text);
        auto read = service.readText();
        QTRY_VERIFY(read.isFinished());
        QCOMPARE(read.result(), text);
        QVERIFY(requestChunks > 1);
        QVERIFY(responseChunks > 1);
        QVERIFY(!requests.transferring());
        QVERIFY(!responses.transferring());
    }
    void clipboardTransferCannotDispatchAnotherService() {
        wire::JsonMessageTransferDecoder decoder({"event", "clipboard", "clipboard"});
        const auto bytes = QJsonDocument(QJsonObject{{"event", "workspaceApplyEdit"}, {"requestId", "1"}}).toJson(QJsonDocument::Compact);
        QString error;
        QVERIFY(!decoder.accept({{"event", "clipboardBegin"}, {"requestId", "1"}, {"length", QString::number(bytes.size())}}, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!decoder.accept({{"event", "clipboardChunk"}, {"requestId", "1"}, {"sequence", "0"}, {"data", QString::fromLatin1(bytes.toBase64())}}, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!decoder.accept({{"event", "clipboardEnd"}, {"requestId", "1"}, {"sequence", "1"}}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!decoder.transferring());
    }
    void orderedReadWritePreserveEmptyAndUnicodeText() {
        QString clipboard;
        WorkbenchClipboard workbench;
        workbench.setBackend([&] { return clipboard; }, [&](const QString& value) { clipboard = value; });
        HostClipboard* host = nullptr;
        HostClipboard service([&](const QJsonObject& request) {
            const auto id = request.value("requestId").toString().toULongLong();
            QVERIFY(workbench.accept(request, [&](const QJsonValue& result, const QString& error) { host->complete(id, result, error); }));
        });
        host = &service;
        const auto text = QString::fromUtf8("unicode \xf0\x9f\x98\x80\n") + QChar(0) + "tail";
        auto written = service.writeText(text);
        auto read = service.readText();
        QTRY_VERIFY(written.isFinished());
        written.waitForFinished();
        QTRY_VERIFY(read.isFinished());
        QCOMPARE(read.result(), text);
        auto cleared = service.writeText({});
        auto empty = service.readText();
        QTRY_VERIFY(cleared.isFinished());
        cleared.waitForFinished();
        QTRY_VERIFY(empty.isFinished());
        QCOMPARE(empty.result(), QString{});
    }
    void backendFailuresAndMalformedResultsReject() {
        WorkbenchClipboard workbench;
        HostClipboard* host = nullptr;
        HostClipboard service([&](const QJsonObject& request) {
            const auto id = request.value("requestId").toString().toULongLong();
            QVERIFY(workbench.accept(request, [&](const QJsonValue& result, const QString& error) { host->complete(id, result, error); }));
        });
        host = &service;
        auto unavailable = service.readText();
        QTRY_VERIFY(unavailable.isFinished());
        QVERIFY_EXCEPTION_THROWN(unavailable.result(), std::exception);
        workbench.setBackend([]() -> QString { throw std::runtime_error("Native read failed."); },
            [](const QString&) { throw std::runtime_error("Native write failed."); });
        auto failed = service.readText();
        QTRY_VERIFY(failed.isFinished());
        QVERIFY_EXCEPTION_THROWN(failed.result(), std::exception);
        auto writeFailed = service.writeText("text");
        QTRY_VERIFY(writeFailed.isFinished());
        QVERIFY_EXCEPTION_THROWN(writeFailed.waitForFinished(), std::exception);
        HostClipboard malformed([&](const QJsonObject&) {});
        auto invalid = malformed.readText();
        malformed.complete(1, true, {});
        QTRY_VERIFY(invalid.isFinished());
        QVERIFY_EXCEPTION_THROWN(invalid.result(), std::exception);
        auto invalidWrite = malformed.writeText("text");
        malformed.complete(2, QString("not an acknowledgement"), {});
        QTRY_VERIFY(invalidWrite.isFinished());
        QVERIFY_EXCEPTION_THROWN(invalidWrite.waitForFinished(), std::exception);
    }
    void shutdownRejectsOperationsWithoutSendingQueuedRequests() {
        int sent = 0;
        HostClipboard service([&](const QJsonObject&) { ++sent; });
        auto pending = service.readText();
        service.shutdown();
        QCoreApplication::processEvents();
        QCOMPARE(sent, 0);
        QTRY_VERIFY(pending.isFinished());
        QVERIFY_EXCEPTION_THROWN(pending.result(), std::exception);
        auto after = service.readText();
        QTRY_VERIFY(after.isFinished());
        QVERIFY_EXCEPTION_THROWN(after.result(), std::exception);
        service.complete(1, "late response", {});
    }
    void malformedRequestsNeverTouchClipboard() {
        int calls = 0;
        WorkbenchClipboard workbench;
        workbench.setBackend([&] { ++calls; return QString{}; }, [&](const QString&) { ++calls; });
        const auto reply = [&](const QJsonValue&, const QString&) { ++calls; };
        QVERIFY(!workbench.accept({{"event", "clipboard"}, {"requestId", "01"}, {"write", false}}, reply));
        QVERIFY(!workbench.accept({{"event", "clipboard"}, {"requestId", "1"}, {"write", true}}, reply));
        QVERIFY(!workbench.accept({{"event", "clipboard"}, {"requestId", "1"}, {"write", "true"}}, reply));
        QCOMPARE(calls, 0);
    }
};
QTEST_GUILESS_MAIN(ExtensionClipboardTests)
#include "tst_extension_clipboard.moc"
