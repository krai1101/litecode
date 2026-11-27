#include "debug/DapProtocol.h"
#include <QSignalSpy>
#include <QtTest>
using namespace litecode::debug;

class DapProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void fragmentedAndConcatenatedFrames() {
        const QJsonObject event{
            {"seq", 1},
            {"type", "event"},
            {"event", "output"},
            {"body",
             QJsonObject{{"output", QString::fromUtf8("Hello \xc3\xa9 \xf0\x9f\x98\x80")}}}};
        const auto frame = DapDecoder::encode(event);
        DapDecoder decoder;
        QVector<QJsonObject> decoded;
        for (const auto byte : frame)
            decoded += decoder.feed(QByteArray(1, byte));
        QCOMPARE(decoded.size(), 1);
        QCOMPARE(decoded[0], event);
        QVERIFY(!decoder.hasPartialMessage());
        decoded = decoder.feed(frame + frame);
        QCOMPARE(decoded.size(), 2);
        QVERIFY(decoder.error().isEmpty());
    }
    void malformedFrames_data() {
        QTest::addColumn<QByteArray>("frame");
        QTest::newRow("missing") << QByteArray("Other: 4\r\n\r\n{}");
        QTest::newRow("duplicate")
            << QByteArray("Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}");
        QTest::newRow("negative") << QByteArray("Content-Length: -1\r\n\r\n");
        QTest::newRow("nondecimal") << QByteArray("Content-Length: +2\r\n\r\n{}");
        QTest::newRow("oversized") << QByteArray("Content-Length: 99999999999\r\n\r\n");
        QTest::newRow("array") << QByteArray("Content-Length: 2\r\n\r\n[]");
        QTest::newRow("invalid-json") << QByteArray("Content-Length: 2\r\n\r\nxx");
        QTest::newRow("oversized-header") << QByteArray(DapDecoder::MaxHeaderBytes + 1, 'x');
    }
    void malformedFrames() {
        QFETCH(QByteArray, frame);
        DapDecoder decoder;
        QVERIFY(decoder.feed(frame).isEmpty());
        QVERIFY(!decoder.error().isEmpty());
        QVERIFY(decoder.feed(DapDecoder::encode({{"seq", 1}})).isEmpty());
        decoder.reset();
        QVERIFY(decoder.error().isEmpty());
        QCOMPARE(decoder.feed(DapDecoder::encode({{"seq", 1}})).size(), 1);
    }
    void correlationsAndStaleSessions() {
        DapChannel channel;
        QSignalSpy writes(&channel, &DapChannel::writeRequested);
        QSignalSpy replies(&channel, &DapChannel::responseReceived);
        QSignalSpy events(&channel, &DapChannel::eventReceived);
        const auto generation = channel.generation();
        const auto first = channel.request("threads");
        const auto second = channel.request("stackTrace");
        QVERIFY(first > 0 && second > first);
        QCOMPARE(writes.size(), 2);
        const QJsonObject response{{"seq", 1},
                                   {"type", "response"},
                                   {"request_seq", second},
                                   {"command", "stackTrace"},
                                   {"success", true},
                                   {"body", QJsonObject{}}};
        channel.receive(DapDecoder::encode(response), generation);
        QCOMPARE(replies.size(), 1);
        QCOMPARE(replies[0][0].toInt(), second);
        channel.receive(DapDecoder::encode(response), generation);
        QCOMPARE(replies.size(), 1);
        channel.reset();
        QCOMPARE(replies.size(), 2);
        QCOMPARE(replies[1][0].toInt(), first);
        QVERIFY(!replies[1][2].toBool());
        const auto event =
            DapDecoder::encode({{"seq", 2}, {"type", "event"}, {"event", "stopped"}});
        channel.receive(event, generation);
        QCOMPARE(events.size(), 0);
        channel.receive(event, channel.generation());
        QCOMPARE(events.size(), 1);
    }
    void mismatchFailsPending() {
        DapChannel channel;
        QSignalSpy errors(&channel, &DapChannel::protocolError);
        QSignalSpy replies(&channel, &DapChannel::responseReceived);
        const auto id = channel.request("threads");
        channel.receive(DapDecoder::encode({{"seq", 1},
                                            {"type", "response"},
                                            {"request_seq", id},
                                            {"command", "continue"},
                                            {"success", true}}),
                        channel.generation());
        QCOMPARE(errors.size(), 1);
        QCOMPARE(replies.size(), 1);
        QVERIFY(!replies[0][2].toBool());
        QCOMPARE(channel.request("threads"), 0);
        channel.reset();
        QVERIFY(channel.request("threads") > 0);
    }
    void timeoutAndReverseRequest() {
        DapChannel channel;
        QSignalSpy replies(&channel, &DapChannel::responseReceived);
        QSignalSpy reverse(&channel, &DapChannel::reverseRequestReceived);
        QSignalSpy writes(&channel, &DapChannel::writeRequested);
        const auto id = channel.request("evaluate", {}, 1);
        QTRY_COMPARE(replies.size(), 1);
        QCOMPARE(replies[0][0].toInt(), id);
        QVERIFY(!replies[0][2].toBool());
        QJsonObject request{{"seq", 5}, {"type", "request"}, {"command", "runInTerminal"}};
        channel.receive(DapDecoder::encode(request), channel.generation());
        QCOMPARE(reverse.size(), 1);
        QVERIFY(channel.respond(request, false, {}, "Not available in this test."));
        DapDecoder decoder;
        const auto response = decoder.feed(writes.last()[0].toByteArray());
        QCOMPARE(response.size(), 1);
        QCOMPARE(response[0].value("request_seq").toInt(), 5);
        QVERIFY(!response[0].value("success").toBool());
    }
};
QTEST_GUILESS_MAIN(DapProtocolTests)
#include "tst_dap_protocol.moc"
