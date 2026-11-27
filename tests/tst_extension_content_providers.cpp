#include "extensions/HostContentProviders.h"
#include "extensions/WorkbenchContentProviders.h"
#include <QtTest>
using namespace litecode::extensions;
class ContentFixture final : public TextDocumentContentProvider {
  public:
    ContentFixture() { onDidChange = changed.event(); }
    ProviderResult<QString> provideTextDocumentContent(const QUrl&, CancellationToken token) override {
        ++calls;
        cancelled = token.isCancellationRequested();
        if (throws) throw std::runtime_error("fixture provider failure");
        return content;
    }
    EventEmitter<QUrl> changed;
    std::optional<QString> content{QString("original")};
    int calls{};
    bool cancelled{}, throws{};
};
class ContentProviderTests final : public QObject {
    Q_OBJECT
  private slots:
    void chunkedTransportPreservesEmptyAbsenceAndUnicode() {
        const auto text = QString(32767, QLatin1Char('x')) + QString::fromUcs4(U"😀") + QString(33000, QLatin1Char('y'));
        wire::TextContentDecoder decoder;
        std::optional<wire::TextContentTransfer> received;
        QString error;
        wire::sendTextContent({1, 2, 3, QUrl("generated:/test"), text, {}, false}, [&](const QJsonObject& message) {
            if (auto value = decoder.accept(message, &error)) received = std::move(value);
        });
        QVERIFY(error.isEmpty());
        QVERIFY(received);
        QCOMPARE(received->content.value(), text);
        received.reset();
        wire::sendTextContent({2, 2, 4, QUrl("generated:/empty"), QString(), {}, false}, [&](const QJsonObject& message) {
            if (auto value = decoder.accept(message, &error)) received = std::move(value);
        });
        QVERIFY(received->content.has_value());
        QVERIFY(received->content->isEmpty());
        received.reset();
        wire::sendTextContent({3, 2, 5, QUrl("generated:/none"), {}, {}, false}, [&](const QJsonObject& message) {
            if (auto value = decoder.accept(message, &error)) received = std::move(value);
        });
        QVERIFY(!received->content.has_value());
    }
    void sameSchemeFallsBackAndDisposalDoesNotCloseMirrors() {
        WorkbenchContentProviders workbench;
        HostDocuments documents({});
        QString transportError;
        HostContentProviders host(documents, [&](const QJsonObject& message) {
            const auto event = message.value("event").toString();
            const auto handle = message.value("handle").toString().toULongLong();
            if (event == "registerTextContentProvider") QVERIFY(workbench.registerProvider("owner", handle, message.value("scheme").toString()));
            else if (event == "unregisterTextContentProvider") QVERIFY(workbench.unregisterProvider("owner", handle));
            else QVERIFY(workbench.accept("owner", message, &transportError));
        });
        connect(&workbench, &WorkbenchContentProviders::contentRequested, &host,
            [&host](const QString&, quint64 handle, quint64 request, const QUrl& uri) { host.provide(handle, request, uri); });
        auto first = std::make_shared<ContentFixture>();
        auto absent = std::make_shared<ContentFixture>(); absent->content.reset();
        auto firstRegistration = host.registerProvider("generated", first);
        auto absentRegistration = host.registerProvider("generated", absent);
        auto requested = workbench.provide(QUrl("generated:/file.py"));
        QTRY_VERIFY(requested.isFinished());
        QCOMPARE(requested.result().value(), QString("original"));
        QCOMPARE(absent->calls, 1);
        QCOMPARE(first->calls, 1);
        QVERIFY(!first->cancelled);
        documents.accept({"1", QUrl("generated:/file.py"), "python", "utf8", "original", 1, false, EndOfLine::LF});
        absentRegistration.dispose();
        firstRegistration.dispose();
        QVERIFY(!documents.find("1")->isClosed());
        auto missing = workbench.provide(QUrl("generated:/new.py"));
        QTRY_VERIFY(missing.isFinished());
        QVERIFY(!missing.result());
        QVERIFY_EXCEPTION_THROWN(host.registerProvider("file", first), std::exception);
        QVERIFY(transportError.isEmpty());
    }
    void updatesIgnoreClosedAndEqualLineContent() {
        WorkbenchContentProviders workbench;
        HostDocuments documents({});
        HostContentProviders host(documents, [&](const QJsonObject& message) {
            if (message.value("event") == "registerTextContentProvider")
                QVERIFY(workbench.registerProvider("owner", message.value("handle").toString().toULongLong(), "generated"));
            else { QString error; QVERIFY2(workbench.accept("owner", message, &error), qPrintable(error)); }
        });
        auto provider = std::make_shared<ContentFixture>();
        auto registration = host.registerProvider("generated", provider);
        const QUrl uri("generated:/test");
        documents.accept({"1", uri, "plaintext", "utf8", "original", 1, false, EndOfLine::LF});
        QSignalSpy changes(&workbench, &WorkbenchContentProviders::virtualDocumentChanged);
        provider->changed.fire(QUrl("another:/test"));
        provider->changed.fire(QUrl("generated:/not-open"));
        provider->changed.fire(uri);
        QTRY_COMPARE(provider->calls, 1);
        QVERIFY(changes.isEmpty());
        provider->content = "changed";
        provider->changed.fire(uri);
        QTRY_COMPARE(changes.size(), 1);
        QCOMPARE(changes.first().at(1).toString(), QString("changed"));
        documents.close("1");
        provider->changed.fire(uri);
        QCoreApplication::processEvents();
        QCOMPARE(provider->calls, 2);
        QCOMPARE(changes.size(), 1);
        host.shutdown(); // Dispose the event subscription before fixture destruction.
    }
    void ownerExitRejectsPendingResolution() {
        WorkbenchContentProviders workbench;
        QVERIFY(workbench.registerProvider("owner", 1, "generated"));
        auto pending = workbench.provide(QUrl("generated:/pending"));
        workbench.removeOwner("owner");
        QTRY_VERIFY(pending.isFinished());
        QVERIFY_EXCEPTION_THROWN(pending.result(), std::exception);
    }
};
QTEST_GUILESS_MAIN(ContentProviderTests)
#include "tst_extension_content_providers.moc"
