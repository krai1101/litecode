#include "extensions/HostExternalUris.h"
#include "extensions/HostUriHandlers.h"
#include "extensions/WorkbenchExternalUris.h"
#include "extensions/WorkbenchUriHandlers.h"
#include "extensions/UriWire.h"
#include <QSignalSpy>
#include <QtTest>
using namespace litecode::extensions;
class RecordingUriHandler final : public UriHandler {
  public:
    QVector<QUrl> received;
    ProviderResult<void> handleUri(const QUrl& uri) override { received.append(uri); return std::monostate{}; }
};
class UriTests final : public QObject {
    Q_OBJECT
  private slots:
    void strictWireValidationAndOwnerScoping() {
        const auto uri = wire::decodeUri("litecode://fixture.uri/auth?token=a%26b#fragment"); QVERIFY(uri);
        QVERIFY(wire::isExtensionUri(*uri, "fixture.uri")); QVERIFY(wire::isExtensionUri(*uri, "FIXTURE.URI"));
        QVERIFY(!wire::isExtensionUri(*uri, "other.uri"));
        QCOMPARE(*wire::decodeUri(wire::encodeUri(*uri)), *uri);
        QVERIFY(!wire::decodeUri("relative/file")); QVERIFY(!wire::decodeUri("https://host/%invalid"));
        QVERIFY(!wire::isExtensionUri(QUrl("litecode://user@fixture.uri/path"), "fixture.uri"));
        QVERIFY(!wire::isExtensionUri(QUrl("litecode://fixture.uri:8080/path"), "fixture.uri"));
    }
    void externalResultsStayWithTheirOriginAndShutdownSettlesFutures() {
        WorkbenchExternalUris backend; HostExternalUris* receiver = nullptr;
        HostExternalUris host([&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture.uri", message, [&](const QJsonObject& reply) { QVERIFY(receiver->accept(reply)); }));
        }); receiver = &host;
        QSignalSpy requests(&backend, &WorkbenchExternalUris::requested);
        auto invalid = host.openExternal(QUrl("command:fixture.run"));
        QVERIFY(invalid.isFinished()); QVERIFY_EXCEPTION_THROWN(invalid.result(), std::exception); QCOMPARE(requests.size(), 0);
        const QUrl target("https://example.invalid/path?q=value#fragment");
        auto resolved = host.asExternalUri(target); QTRY_COMPARE(requests.size(), 1);
        const auto request = requests.first().first().value<ExternalUriRequest>();
        QCOMPARE(request.uri, target); QVERIFY(!request.open);
        QVERIFY(!backend.complete("other.uri", request.id, target.toString()));
        QVERIFY(!backend.complete(request.owner, request.id, true));
        QVERIFY(backend.complete(request.owner, request.id, wire::encodeUri(target)));
        QTRY_VERIFY(resolved.isFinished()); QCOMPARE(resolved.result(), target);
        auto opened = host.openExternal(target); QTRY_COMPARE(requests.size(), 2);
        const auto open = requests.last().first().value<ExternalUriRequest>();
        QVERIFY(backend.complete(open.owner, open.id, false)); QTRY_VERIFY(opened.isFinished()); QVERIFY(!opened.result());
        auto pending = host.asExternalUri(target); host.shutdown();
        QVERIFY(pending.isFinished()); QVERIFY_EXCEPTION_THROWN(pending.result(), std::exception);
        QVERIFY(!host.accept({{"requestId", "01"}, {"error", ""}}));
    }
    void registrationDisposalAndBufferedActivationDeliverOriginalUri() {
        WorkbenchUriHandlers backend; HostUriHandlers* receiver = nullptr;
        HostUriHandlers host("fixture.uri", [&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture.uri", message, [&](const QJsonObject& reply) { QVERIFY(receiver->accept(reply)); }));
        }); receiver = &host;
        QSignalSpy activations(&backend, &WorkbenchUriHandlers::activationRequested);
        const QUrl original("litecode://fixture.uri/auth?token=a%26b&state=x#fragment");
        QVERIFY(backend.dispatch("fixture.uri", original)); QCOMPARE(activations.size(), 1);
        auto handler = std::make_shared<RecordingUriHandler>();
        auto registration = host.registerUriHandler(handler);
        QVERIFY_EXCEPTION_THROWN(host.registerUriHandler(handler), std::exception);
        QTRY_COMPARE(handler->received.size(), 1); QCOMPARE(handler->received.first(), original);
        QVERIFY(!backend.dispatch("other.uri", original));
        QVERIFY(!host.accept({{"handle", "1"}, {"uri", "litecode://other.uri/auth"}}));
        registration.dispose();
        QCoreApplication::processEvents();
        QVERIFY(backend.dispatch("fixture.uri", original)); QCOMPARE(handler->received.size(), 1);
        auto replacement = std::make_shared<RecordingUriHandler>();
        auto next = host.registerUriHandler(replacement);
        QTRY_COMPARE(replacement->received.size(), 1);
        next.dispose(); host.shutdown(); backend.removeOwner("fixture.uri");
        QVERIFY_EXCEPTION_THROWN(host.registerUriHandler(handler), std::exception);
    }
    void ownerRemovalRejectsPendingExternalResults() {
        WorkbenchExternalUris backend; HostExternalUris* receiver = nullptr;
        HostExternalUris host([&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture.uri", message, [&](const QJsonObject& reply) { QVERIFY(receiver->accept(reply)); }));
        }); receiver = &host;
        QSignalSpy requests(&backend, &WorkbenchExternalUris::requested);
        auto pending = host.asExternalUri(QUrl("https://example.invalid/")); QTRY_COMPARE(requests.size(), 1);
        backend.removeOwner("fixture.uri"); QTRY_VERIFY(pending.isFinished());
        QVERIFY_EXCEPTION_THROWN(pending.result(), std::exception);
        const auto request = requests.first().first().value<ExternalUriRequest>();
        QVERIFY(!backend.complete(request.owner, request.id, "https://late.invalid/"));
    }
};
QTEST_GUILESS_MAIN(UriTests)
#include "tst_extension_uris.moc"
