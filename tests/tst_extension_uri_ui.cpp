#include "extensions/ExtensionManager.h"
#include "extensions/UriWire.h"
#include "ui/extensions/ExternalUriService.h"
#include "ui/extensions/NativeUriBroker.h"
#include "ui/extensions/NativeUriDispatcher.h"
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtConcurrentRun>
#include <QtTest>
using namespace litecode::extensions;
class UriUiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settingsProfile_;
  private slots:
    void initTestCase() {
        QVERIFY(settingsProfile_.isValid());
        QCoreApplication::setOrganizationName("LiteCodeTests");
        QCoreApplication::setApplicationName("UriUiTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsProfile_.path());
    }
    void dispatcherBuffersUntilWindowServiceIsReady() {
        auto& dispatcher = litecode::ui::NativeUriDispatcher::forApplication();
        const QUrl uri("litecode://fixture.uri/auth?value=a%26b#state");
        QVERIFY(dispatcher.dispatchUri(uri));
        QVector<QUrl> received;
        QWidget window;
        QObject lifetime;
        dispatcher.registerWindow(window, lifetime, [&](const QUrl& callback) {
            received.append(callback);
            return true;
        });
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(received.first().toEncoded(), uri.toEncoded());
    }
    void desktopBrokerRoutesAcrossLocalEndpointsAndIsolatesProfiles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString profile = directory.filePath("settings.ini");
        litecode::ui::NativeUriBroker first(profile), second(profile);
        QVector<QUrl> firstCalls, secondCalls;
        QVERIFY(first.listen([&](const QUrl& uri) {
            firstCalls.append(uri);
            return true;
        }));
        QVERIFY(second.listen([&](const QUrl& uri) {
            secondCalls.append(uri);
            return true;
        }));
        // Avoid relying on wall-clock resolution for activation ordering.
        for (int index = 0; index < 16; ++index)
            first.activated();
        const QList<QUrl> uris{QUrl("litecode://fixture.uri/auth?state=a%26b#fragment")};
        auto routed = QtConcurrent::run(
            [profile, uris] { return litecode::ui::NativeUriBroker::forward(profile, uris); });
        QTRY_VERIFY(routed.isFinished());
        QCOMPARE(routed.result(), qsizetype(1));
        QCOMPARE(firstCalls, QVector<QUrl>{uris.first()});
        QVERIFY(secondCalls.isEmpty());
        auto isolated = QtConcurrent::run([profile, uris] {
            return litecode::ui::NativeUriBroker::forward(profile + ".other", uris);
        });
        QTRY_VERIFY(isolated.isFinished());
        QCOMPARE(isolated.result(), qsizetype(0));
    }
    void desktopBrokerRejectionFallsBackWithoutLosingCallback() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString profile = directory.filePath("settings.ini");
        litecode::ui::NativeUriBroker accepting(profile), rejecting(profile);
        int accepted = 0, rejected = 0;
        QVERIFY(accepting.listen([&](const QUrl&) {
            ++accepted;
            return true;
        }));
        QVERIFY(rejecting.listen([&](const QUrl&) {
            ++rejected;
            return false;
        }));
        for (int index = 0; index < 16; ++index)
            rejecting.activated();
        auto routed = QtConcurrent::run([profile] {
            return litecode::ui::NativeUriBroker::forward(
                profile, {QUrl("litecode://fixture.uri/callback")});
        });
        QTRY_VERIFY(routed.isFinished());
        QCOMPARE(routed.result(), qsizetype(1));
        QCOMPARE(accepted, 1);
        QCOMPARE(rejected, 1);
    }
    void localResolutionUsesDesktopNoOpAndWorkspaceUriWrapping() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ExtensionManager manager(directory.path());
        QWidget window;
        litecode::ui::ExternalUriService service(manager, &window, &window);
        QVector<QJsonObject> replies;
        const auto reply = [&](const QJsonObject& message) { replies.append(message); };
        const QUrl original("https://example.invalid/path?q=value#fragment");
        QVERIFY(manager.externalUris().accept(
            "fixture.uri",
            {{"requestId", "1"}, {"open", false}, {"uri", wire::encodeUri(original)}}, reply));
        QCOMPARE(replies.size(), 1);
        QCOMPARE(*wire::decodeUri(replies.first().value("result")), original);
        const QUrl workspace = QUrl::fromLocalFile(directory.path());
        QVERIFY(manager.externalUris().accept(
            "fixture.uri",
            {{"requestId", "2"}, {"open", false}, {"uri", wire::encodeUri(workspace)}}, reply));
        QCOMPARE(replies.size(), 2);
        const auto resolved = wire::decodeUri(replies.last().value("result"));
        QVERIFY(resolved);
        QCOMPARE(resolved->scheme(), QString("litecode"));
        QCOMPARE(resolved->authority(), QString("workspace"));
        QVERIFY(!service.handleIncoming(QUrl("other://fixture.uri/auth")));
    }
    void incomingExtensionUriRequiresConfirmationAndCanBeCancelled() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QWidget window;
        window.resize(800, 600);
        window.show();
        litecode::ui::ExternalUriService service(manager, &window, &window);
        QSignalSpy dispatched(&manager, &ExtensionManager::uriDispatched);
        QVERIFY(service.handleIncoming(QUrl("litecode://tests.native/auth?token=fixture-value")));
        QTRY_VERIFY(window.findChild<QDialog*>());
        QCOMPARE(dispatched.size(), 0);
        window.findChild<QDialog*>()->reject();
        QCoreApplication::processEvents();
        QCOMPARE(dispatched.size(), 0);
        const QUrl original("litecode://tests.native/auth?token=a%26b&state=fixture#fragment");
        QVERIFY(service.handleIncoming(original));
        QTRY_VERIFY(window.findChild<QDialog*>());
        window.findChild<QDialog*>()->accept();
        QTRY_COMPARE_WITH_TIMEOUT(dispatched.size(), 1, 5000);
        QSignalSpy replies(&manager, &ExtensionManager::commandResult);
        const auto requestId = manager.requestCommand("tests.native.receivedUris");
        QVERIFY(requestId);
        if (replies.isEmpty())
            QVERIFY(replies.wait(5000));
        QCOMPARE(replies.first()[0].toULongLong(), requestId);
        QVERIFY(replies.first()[2].toString().isEmpty());
        QCOMPARE(replies.first()[1].value<QJsonValue>().toArray(),
                 QJsonArray{wire::encodeUri(original)});
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(!service.handleIncoming(original));
    }
    void dispatcherSelectsTheMostRecentlyActiveWindow() {
        QVector<int> calls;
        QWidget first, second;
        QObject firstLifetime, secondLifetime;
        auto& dispatcher = litecode::ui::NativeUriDispatcher::forApplication();
        dispatcher.registerWindow(first, firstLifetime, [&](const QUrl&) {
            calls.append(1);
            return true;
        });
        dispatcher.registerWindow(second, secondLifetime, [&](const QUrl&) {
            calls.append(2);
            return true;
        });
        const QUrl uri("litecode://fixture.uri/callback");
        dispatcher.dispatchUri(uri);
        QCOMPARE(calls, QVector<int>{2});
        QEvent activate(QEvent::WindowActivate);
        QApplication::sendEvent(&first, &activate);
        dispatcher.dispatchUri(uri);
        QCOMPARE(calls, (QVector<int>{2, 1}));
    }
};
QTEST_MAIN(UriUiTests)
#include "tst_extension_uri_ui.moc"
