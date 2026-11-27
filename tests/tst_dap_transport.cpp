#include "debug/DapTransport.h"
#include "SocketAdapterFixture.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode::debug;
class DapTransportTests final : public QObject {
    Q_OBJECT
  private slots:
    void executableLookupUsesAdapterEnvironment() {
        AdapterProcess configuration;
        configuration.executable = QFileInfo(LITECODE_DAP_ADAPTER_FIXTURE).fileName();
        configuration.environment.insert("PATH", QFileInfo(LITECODE_DAP_ADAPTER_FIXTURE).absolutePath()
            + QDir::listSeparator() + configuration.environment.value("PATH"));
        DapTransport transport;
        QSignalSpy started(&transport, &DapTransport::started), stopped(&transport, &DapTransport::stopped);
        QSignalSpy replies(&transport, &DapTransport::responseReceived);
        QVERIFY(transport.start(configuration)); QTRY_COMPARE(started.size(), 1);
        QVERIFY(transport.request("echo", {{"value", "custom-path"}}));
        QTRY_COMPARE(replies.size(), 1); QVERIFY(replies[0][2].toBool());
        transport.stop(); QTRY_COMPARE(stopped.size(), 1);
        configuration.environment.insert("PATH", "");
        QVERIFY(!transport.start(configuration));
        QCOMPARE(transport.state(), DapTransport::State::Idle);
    }
    void unavailableSocketEndpointsFailOnce() {
        QTcpServer unused;
        QVERIFY(unused.listen(QHostAddress::LocalHost, 0));
        const auto port = unused.serverPort(); unused.close();
        DapTransport transport;
        QSignalSpy stopped(&transport, &DapTransport::stopped), errors(&transport, &DapTransport::errorOccurred);
        QVERIFY(transport.startServer({port}));
        QTRY_COMPARE(stopped.size(), 1); QCOMPARE(errors.size(), 1);
        QCOMPARE(transport.state(), DapTransport::State::Idle);
        QTemporaryDir directory;
        QVERIFY(transport.startNamedPipeServer({directory.filePath("no-adapter")}));
        QTRY_COMPARE(stopped.size(), 2); QCOMPARE(errors.size(), 2);
        QCOMPARE(transport.state(), DapTransport::State::Idle);
        QVERIFY(!stopped.last()[1].toBool());
    }
    void socketLifecycleAndCorrelation_data() {
        QTest::addColumn<bool>("local");
        QTest::newRow("tcp") << false;
        QTest::newRow("named-pipe-or-unix-socket") << true;
    }
    void socketLifecycleAndCorrelation() {
        QFETCH(bool, local);
        QTcpServer tcp;
        QLocalServer pipe;
        QTemporaryDir directory;
        const auto path = directory.filePath(QUuid::createUuid().toString(QUuid::Id128));
        if (local) QVERIFY(pipe.listen(path));
        else QVERIFY(tcp.listen(QHostAddress::LocalHost, 0));
        std::unique_ptr<SocketAdapterFixture> peer;
        QPointer<QIODevice> socket;
        const auto accepted = [&](QIODevice* incoming) {
            socket = incoming; peer = std::make_unique<SocketAdapterFixture>(incoming);
        };
        connect(&tcp, &QTcpServer::newConnection, this, [&] { accepted(tcp.nextPendingConnection()); });
        connect(&pipe, &QLocalServer::newConnection, this, [&] { accepted(pipe.nextPendingConnection()); });
        DapTransport transport;
        QSignalSpy started(&transport, &DapTransport::started);
        QSignalSpy stopped(&transport, &DapTransport::stopped);
        QSignalSpy replies(&transport, &DapTransport::responseReceived);
        QSignalSpy reverse(&transport, &DapTransport::reverseRequestReceived);
        QSignalSpy errors(&transport, &DapTransport::errorOccurred);
        const auto start = [&] {
            return local ? transport.startNamedPipeServer({pipe.fullServerName()})
                         : transport.startServer({tcp.serverPort()});
        };
        for (int iteration = 0; iteration < 2; ++iteration) {
            QVERIFY(start()); QVERIFY(!start());
            QTRY_COMPARE(started.size(), iteration + 1);
            QTRY_VERIFY(peer && socket);
            const auto arguments = QJsonObject{{"text", "quotes \" & | and Unicode \u03bb"}};
            const auto id = transport.request("echo", arguments);
            QVERIFY(id);
            QTRY_COMPARE(replies.size(), iteration * 3 + 1);
            QCOMPARE(replies.last()[0].toInt(), id);
            QCOMPARE(replies.last()[3].value<QJsonObject>().value("echo").toObject(), arguments);
            QVERIFY(transport.request("reverse"));
            QTRY_COMPARE(reverse.size(), iteration + 1);
            QVERIFY(transport.respond(reverse.last()[0].value<QJsonObject>(), true, {{"processId", 123}}));
            QTRY_VERIFY(!peer->reverseResponse.isEmpty());
            QCOMPARE(peer->reverseResponse.value("body").toObject().value("processId").toInt(), 123);
            QTRY_COMPARE(replies.size(), iteration * 3 + 2);
            const auto pending = transport.request("hold"); QVERIFY(pending);
            transport.stop();
            QCOMPARE(stopped.size(), iteration + 1);
            QCOMPARE(transport.state(), DapTransport::State::Idle);
            QCOMPARE(replies.size(), iteration * 3 + 3);
            QCOMPARE(replies.last()[0].toInt(), pending); QVERIFY(!replies.last()[2].toBool());
            QVERIFY(!stopped.last()[1].toBool());
            QVERIFY(local ? pipe.isListening() : tcp.isListening());
            peer.reset(); socket.clear();
        }
        QVERIFY(errors.isEmpty());
    }
    void socketFailureSettlesRequests_data() {
        QTest::addColumn<bool>("local"); QTest::addColumn<bool>("malformed");
        QTest::newRow("tcp-close") << false << false;
        QTest::newRow("pipe-close") << true << false;
        QTest::newRow("tcp-malformed") << false << true;
        QTest::newRow("pipe-malformed") << true << true;
    }
    void socketFailureSettlesRequests() {
        QFETCH(bool, local); QFETCH(bool, malformed);
        QTcpServer tcp; QLocalServer pipe; QTemporaryDir directory;
        const auto path = directory.filePath(QUuid::createUuid().toString(QUuid::Id128));
        if (local) QVERIFY(pipe.listen(path)); else QVERIFY(tcp.listen(QHostAddress::LocalHost, 0));
        std::unique_ptr<SocketAdapterFixture> peer;
        QPointer<QIODevice> socket;
        const auto accepted = [&](QIODevice* incoming) { socket = incoming; peer = std::make_unique<SocketAdapterFixture>(incoming); };
        connect(&tcp, &QTcpServer::newConnection, this, [&] { accepted(tcp.nextPendingConnection()); });
        connect(&pipe, &QLocalServer::newConnection, this, [&] { accepted(pipe.nextPendingConnection()); });
        DapTransport transport;
        QSignalSpy started(&transport, &DapTransport::started), stopped(&transport, &DapTransport::stopped);
        QSignalSpy replies(&transport, &DapTransport::responseReceived), errors(&transport, &DapTransport::errorOccurred);
        QVERIFY(local ? transport.startNamedPipeServer({pipe.fullServerName()}) : transport.startServer({tcp.serverPort()}));
        QTRY_COMPARE(started.size(), 1); QTRY_VERIFY(socket);
        const auto pending = transport.request("hold"); QVERIFY(pending);
        if (malformed) QVERIFY(transport.request("malformed"));
        else if (local) qobject_cast<QLocalSocket*>(socket.data())->abort();
        else qobject_cast<QTcpSocket*>(socket.data())->abort();
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(replies.size(), malformed ? 2 : 1);
        for (const auto& reply : replies) QVERIFY(!reply[2].toBool());
        QCOMPARE(transport.state(), DapTransport::State::Idle);
        QVERIFY(!stopped[0][1].toBool()); // disconnected remote server is not a crashed child
    }
    void cancelSocketStartupAndRejectInvalidEndpoints() {
        DapTransport transport;
        QSignalSpy stopped(&transport, &DapTransport::stopped);
        QVERIFY(!transport.startServer({0}));
        QVERIFY(!transport.startNamedPipeServer({{}}));
        QVERIFY(!transport.startServer({1, "127.0.0.1", 0}));
        connect(&transport, &DapTransport::stateChanged, &transport, [&](DapTransport::State state) {
            if (state == DapTransport::State::Starting) transport.stop();
        });
        QVERIFY(!transport.startServer({1}));
        QCOMPARE(stopped.size(), 1);
        QCOMPARE(transport.state(), DapTransport::State::Idle);
    }
    void validatesWithoutLaunching() {
        DapTransport transport;
        QSignalSpy errors(&transport, &DapTransport::errorOccurred);
        QVERIFY(!transport.start({}));
        AdapterProcess configuration;
        configuration.executable = "./untrusted-adapter";
        QVERIFY(!transport.start(configuration));
        QCOMPARE(errors.size(), 2);
        QCOMPARE(transport.state(), DapTransport::State::Idle);
        QCOMPARE(transport.request("initialize"), 0);
    }
    void processLifecycleAndEcho() {
        DapTransport transport;
        QSignalSpy started(&transport, &DapTransport::started);
        QSignalSpy stopped(&transport, &DapTransport::stopped);
        QSignalSpy replies(&transport, &DapTransport::responseReceived);
        AdapterProcess configuration;
        configuration.executable = LITECODE_DAP_ADAPTER_FIXTURE;
        for (int iteration = 0; iteration < 3; ++iteration) {
            QVERIFY(transport.start(configuration));
            QVERIFY(!transport.start(configuration));
            QTRY_COMPARE(started.size(), iteration + 1);
            const QJsonObject arguments{{"value", "literal quotes ' \" & | ;"}};
            const auto id = transport.request("echo", arguments);
            QVERIFY(id > 0);
            QTRY_COMPARE(replies.size(), iteration + 1);
            QCOMPARE(replies.last()[0].toInt(), id);
            QVERIFY(replies.last()[2].toBool());
            QCOMPARE(qvariant_cast<QJsonObject>(replies.last()[3]).value("echo").toObject(),
                     arguments);
            transport.stop();
            QTRY_COMPARE(stopped.size(), iteration + 1);
            QVERIFY(!stopped.last()[1].toBool());
            QCOMPARE(transport.state(), DapTransport::State::Idle);
        }
    }
    void exitFailsPendingRequests() {
        DapTransport transport;
        QSignalSpy started(&transport, &DapTransport::started);
        QSignalSpy stopped(&transport, &DapTransport::stopped);
        QSignalSpy replies(&transport, &DapTransport::responseReceived);
        AdapterProcess configuration;
        configuration.executable = LITECODE_DAP_ADAPTER_FIXTURE;
        QVERIFY(transport.start(configuration));
        QTRY_COMPARE(started.size(), 1);
        QVERIFY(transport.request("hold") > 0);
        QVERIFY(transport.request("exit") > 0);
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(replies.size(), 2);
        QVERIFY(!replies[0][2].toBool());
        QVERIFY(!replies[1][2].toBool());
    }
    void malformedOutputStopsAdapter() {
        DapTransport transport;
        QSignalSpy started(&transport, &DapTransport::started);
        QSignalSpy stopped(&transport, &DapTransport::stopped);
        QSignalSpy errors(&transport, &DapTransport::errorOccurred);
        AdapterProcess configuration;
        configuration.executable = LITECODE_DAP_ADAPTER_FIXTURE;
        QVERIFY(transport.start(configuration));
        QTRY_COMPARE(started.size(), 1);
        QVERIFY(transport.request("malformed") > 0);
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(transport.state(), DapTransport::State::Idle);
    }
};
QTEST_GUILESS_MAIN(DapTransportTests)
#include "tst_dap_transport.moc"
