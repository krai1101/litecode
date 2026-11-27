#include "extensions/ExtensionManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>
#include <algorithm>
using namespace litecode::extensions;
class ExtensionTests : public QObject {
    Q_OBJECT
  private slots:
    void optionalChatHandlerResultsCrossSeparateHost() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST); manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        QSignalSpy followups(&manager, &ExtensionManager::chatFollowupsReady);
        const QStringList forms{"absentResult", "optionalResult", "asyncAbsentResult", "asyncOptionalResult"};
        for (int index = 0; index < forms.size(); ++index) {
            const auto request = manager.requestChat("optional-result-" + QString::number(index), "tests.native.assistant", {{"prompt", forms.at(index)}});
            QVERIFY(request != 0); QTRY_COMPARE(finished.size(), index + 1); QTRY_COMPARE(followups.size(), index + 1);
            QCOMPARE(finished.last().at(0).toULongLong(), request); QVERIFY(finished.last().at(2).toString().isEmpty());
            QCOMPARE(finished.last().at(1).value<QJsonObject>().value("metadata").toObject().value("optional").toBool(),
                !forms.at(index).contains("Absent", Qt::CaseInsensitive));
        }
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void participantPropertiesAndOptionalFollowupsCrossSeparateHost() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST); manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy commands(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.participantProperties") != 0);
        QTRY_COMPARE(commands.size(), 1); QVERIFY(commands.first().at(2).toString().isEmpty());
        const auto properties = commands.first().at(1).value<QJsonValue>().toObject();
        QCOMPARE(properties.value("id").toString(), QString("tests.native.assistant"));
        QVERIFY(properties.value("requestHandler").toBool()); QVERIFY(properties.value("followupProvider").toBool());
        QVERIFY(properties.value("iconPath").toBool());
        QTRY_VERIFY(manager.chatParticipantContributions().first().hasFollowups);
        QSignalSpy followups(&manager, &ExtensionManager::chatFollowupsReady);
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        const QStringList forms{"absentFollowups", "optionalFollowups", "asyncAbsentFollowups", "asyncOptionalFollowups"};
        for (int index = 0; index < forms.size(); ++index) {
            const auto request = manager.requestChat("optional-followups-" + QString::number(index), "tests.native.assistant", {{"prompt", forms.at(index)}});
            QVERIFY(request != 0);
            QTRY_COMPARE(finished.size(), index + 1); QTRY_COMPARE(followups.size(), index + 1);
            QCOMPARE(followups.last().at(0).toULongLong(), request); QVERIFY(followups.last().at(2).toString().isEmpty());
            QCOMPARE(followups.last().at(1).value<QJsonArray>().size(), forms.at(index).contains("Absent", Qt::CaseInsensitive) ? 0 : 1);
        }
        QVERIFY(manager.requestCommand("tests.native.clearFollowupProvider") != 0);
        QTRY_COMPARE(commands.size(), 2); QTRY_VERIFY(!manager.chatParticipantContributions().first().hasFollowups);
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void windowStateInitializationAndEventsCrossSeparateHost() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST); manager.discover();
        manager.setWindowState({false, false});
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.windowState") != 0);
        QTRY_COMPARE(results.size(), 1); QVERIFY(results.last().at(2).toString().isEmpty());
        const auto initial = results.last().at(1).value<QJsonValue>().toObject();
        QCOMPARE(initial.value("focused"), QJsonValue(false)); QCOMPARE(initial.value("active"), QJsonValue(false));
        QVERIFY(initial.value("events").toArray().isEmpty());
        manager.setWindowState({true, false}); manager.setWindowState({true, true}); manager.setWindowState({true, true});
        QVERIFY(manager.requestCommand("tests.native.windowState") != 0);
        QTRY_COMPARE(results.size(), 2); QVERIFY(results.last().at(2).toString().isEmpty());
        const auto updated = results.last().at(1).value<QJsonValue>().toObject();
        QCOMPARE(updated.value("events").toArray(), (QJsonArray{
            QJsonObject{{"focused", true}, {"active", false}}, QJsonObject{{"focused", true}, {"active", true}}}));
        QCOMPARE(updated.value("focused"), QJsonValue(true)); QCOMPARE(updated.value("active"), QJsonValue(true));
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void uriResolutionAndDeliveryCrossSeparateHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST); manager.discover();
        connect(&manager.externalUris(), &WorkbenchExternalUris::requested, &manager, [&](const ExternalUriRequest& request) {
            QVERIFY(!request.open);
            QVERIFY(manager.externalUris().complete(request.owner, request.id, request.uri.toString(QUrl::FullyEncoded)));
        });
        QVERIFY(manager.setEnabled("tests.native", true));
        const QUrl callback("litecode://tests.native/auth?token=fixture-token&state=a%26b#fragment");
        QSignalSpy dispatched(&manager, &ExtensionManager::uriDispatched);
        QVERIFY(manager.dispatchUri(callback));
        QTRY_COMPARE(dispatched.size(), 1);
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.receivedUris", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY2(results.first().at(2).toString().isEmpty(), qPrintable(results.first().at(2).toString()));
        QVERIFY(!results.first().at(1).value<QJsonValue>().toArray().isEmpty());
        QCOMPARE(results.first().at(1).value<QJsonValue>().toArray().first().toString(), callback.toString(QUrl::FullyEncoded));
        QVERIFY(manager.requestCommand("tests.native.resolveUri", {"https://example.invalid/resource"}) != 0);
        QTRY_COMPARE(results.size(), 2);
        QVERIFY(results.last().at(2).toString().isEmpty());
        QCOMPARE(results.last().at(1).value<QJsonValue>().toString(), QString("https://example.invalid/resource"));
        QVERIFY(manager.setEnabled("tests.native", false)); QVERIFY(!manager.uriTarget(callback));
    }
    void statusBarUpdatesAndCommandArgumentsCrossSeparateHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.statusBar", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY2(results.first().at(2).toString().isEmpty(), qPrintable(results.first().at(2).toString()));
        QCOMPARE(results.first().at(1).value<QJsonValue>().toObject().value("id").toString(), QString("fixture"));
        QTRY_COMPARE(manager.statusBar().entries().size(), 1);
        const auto entry = manager.statusBar().entries().first();
        QCOMPARE(entry.id, QString("tests.native.fixture"));
        QCOMPARE(entry.state.text, QString("$(zap) Fixture"));
        QCOMPARE(entry.state.alignment, StatusBarAlignment::Right);
        QVERIFY(manager.statusBar().invoke(entry.key));
        QTRY_COMPARE(results.size(), 2);
        QVERIFY2(results.last().at(2).toString().isEmpty(), qPrintable(results.last().at(2).toString()));
        QCOMPARE(results.last().at(1).value<QJsonValue>().toInt(), 42);
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(manager.statusBar().entries().isEmpty());
    }
    void quickPickPreservesTypedPayloadAcrossSeparateHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        connect(&manager.quickPick(), &WorkbenchQuickPick::itemsChanged, &manager,
            [&](const QString& owner, quint64 id, const QVector<QuickPickItem>& items) {
                QCOMPARE(items.size(), 2); QCOMPARE(items.first().label, items.last().label);
                QVERIFY(manager.quickPick().complete(owner, id, QVector<int>{1}));
            });
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.quickPick", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY2(results.first().at(2).toString().isEmpty(), qPrintable(results.first().at(2).toString()));
        QCOMPARE(results.first().at(1).value<QJsonValue>().toInt(), 22);
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void inputBoxValidationAndResultCrossSeparateHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        InputBoxRequest request;
        connect(&manager.inputBox(), &WorkbenchInputBox::requested, &manager, [&](const InputBoxRequest& value) {
            request = value;
            QVERIFY(value.hasValidation);
            QVERIFY(manager.inputBox().validate(value.owner, value.id, value.options.value.value_or(QString{})) != 0);
        });
        connect(&manager.inputBox(), &WorkbenchInputBox::validated, &manager,
            [&](const QString& owner, quint64 id, quint64, const InputBoxValidationResult& value, const QString& error) {
                QVERIFY(error.isEmpty());
                const auto* message = std::get_if<InputBoxValidationMessage>(&value); QVERIFY(message);
                QCOMPARE(message->severity, InputBoxValidationSeverity::Warning);
                QVERIFY(manager.inputBox().complete(owner, id, request.options.value.value_or(QString{})));
            });
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.inputBox", {"  warning  "}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY2(results.first().at(2).toString().isEmpty(), qPrintable(results.first().at(2).toString()));
        QCOMPARE(results.first().at(1).value<QJsonValue>().toString(), QString("  warning  "));
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void stablePrimitivesWorkInsideSeparateNativeHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.primitives", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY2(results.first().at(2).toString().isEmpty(), qPrintable(results.first().at(2).toString()));
        const auto value = results.first().at(1).value<QJsonValue>().toObject();
        QVERIFY(value.value("fixedNone").toBool());
        QCOMPARE(value.value("oldCalls").toInt(), 0);
        QCOMPARE(value.value("newCalls").toInt(), 1);
        QVERIFY(value.value("cancelled").toBool());
        QVERIFY(value.value("disposedCopies").toBool());
        QCOMPARE(value.value("events").toArray(), QJsonArray({"first:1", "second:1", "first:2", "second:2"}));
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void gracefulShutdownDeactivatesBeforeSubscriptionsDespiteExceptions() {
        for (const bool throwing : {false, true}) {
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
            ExtensionManager manager(directory.path());
            manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
            manager.discover();
            QVERIFY(manager.setEnabled("tests.native", true));
            QSignalSpy results(&manager, &ExtensionManager::commandResult);
            const auto marker = directory.filePath("lifecycle.txt");
            QVERIFY(manager.requestCommand("tests.native.lifecycle", {marker, throwing}) != 0);
            QTRY_COMPARE(results.size(), 1);
            QVERIFY2(results.first().at(2).toString().isEmpty(), qPrintable(results.first().at(2).toString()));
            QVERIFY(manager.setEnabled("tests.native", false));
            const auto contents = [&] {
                QFile file(marker);
                return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
            };
            QTRY_COMPARE(contents(), QByteArray("deactivate\ncontext:available\nsubscription:1\nsubscription:2\n"));
        }
    }
    void environmentIdentitySurvivesSeparateHostRestart() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setDefaultShell("/fixture/first-shell");
        manager.outputs().setGlobalLogLevel(LogLevel::Warning);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.environment", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        auto result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        const auto first = result.at(1).value<QJsonValue>().toObject();
        QVERIFY(!first.value("machineId").toString().isEmpty());
        QVERIFY(!first.value("sessionId").toString().isEmpty());
        QCOMPARE(first.value("appName").toString(), QCoreApplication::applicationName());
        QCOMPARE(first.value("appRoot").toString(), QCoreApplication::applicationDirPath());
        QCOMPARE(first.value("uiKind").toInt(), 1);
        QVERIFY(!first.contains("remoteName"));
        QCOMPARE(first.value("shell").toString(), QString("/fixture/first-shell"));
        QCOMPARE(first.value("logLevel").toInt(), static_cast<int>(LogLevel::Warning));
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(manager.setEnabled("tests.native", true));
        QVERIFY(manager.requestCommand("tests.native.environment", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        QCOMPARE(result.at(1).value<QJsonValue>().toObject(), first);
        manager.setDefaultShell("/fixture/second-shell");
        QVERIFY(manager.requestCommand("tests.native.environment", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("shell").toString(), QString("/fixture/second-shell"));
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("shellChanges").toArray(), QJsonArray{"/fixture/second-shell"});
        manager.outputs().setGlobalLogLevel(LogLevel::Trace);
        QVERIFY(manager.requestCommand("tests.native.environment", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("logLevel").toInt(), static_cast<int>(LogLevel::Trace));
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("logLevelChanges").toArray(), QJsonArray{static_cast<int>(LogLevel::Trace)});
        manager.setDefaultShell("/fixture/second-shell");
        QVERIFY(manager.requestCommand("tests.native.environment", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("shellChanges").toArray(), QJsonArray{"/fixture/second-shell"});
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void clipboardUsesWorkbenchBackendAcrossSeparateHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        QString clipboard = "original";
        int reads = 0, writes = 0;
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.clipboard().setBackend([&] { ++reads; return clipboard; }, [&](const QString& text) { ++writes; clipboard = text; });
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.clipboard", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        auto result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        QCOMPARE(result.at(1).value<QJsonValue>().toString(), QString("original"));
        const auto text = QString::fromUtf8("host text \xf0\x9f\x98\x80");
        QVERIFY(manager.requestCommand("tests.native.clipboard", {text}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QVERIFY(result.at(1).value<QJsonValue>().toBool());
        QCOMPARE(clipboard, text);
        QCOMPARE(writes, 1);
        QVERIFY(manager.requestCommand("tests.native.clipboard", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toString(), text);
        QCOMPARE(reads, 2);
        // Construct large content inside the plugin so the test also isolates
        // clipboard transport from the separate command-argument/result limit.
        const auto large = QString(9 * 1024 * 1024, QLatin1Char('x')) + QString::fromUtf8("\n\xf0\x9f\x98\x80");
        QVERIFY(manager.requestCommand("tests.native.clipboard", {QJsonObject{{"length", 9 * 1024 * 1024}}}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        QVERIFY(result.at(1).value<QJsonValue>().toBool());
        QCOMPARE(clipboard, large);
        QVERIFY(manager.requestCommand("tests.native.clipboard", {QJsonObject{{"measure", true}}}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        const auto measured = result.at(1).value<QJsonValue>().toObject();
        QCOMPARE(measured.value("length").toDouble(), static_cast<double>(large.size()));
        QCOMPARE(measured.value("tail").toString(), large.right(4));
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void documentOpeningResolvesToSynchronizedHostObject() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        int opens = 0;
        QString openingOwner;
        QStringList removedOwners;
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        manager.documents().setOpenDelegate(
            [&manager, &opens, &openingOwner](const QString& owner, const QUrl& uri, const TextDocumentOpenOptions&) {
                ++opens;
                openingOwner = owner;
                QPromise<TextDocumentSnapshot> promise;
                promise.start();
                TextDocumentSnapshot snapshot{"73", uri, "python", "utf8", "fixture text", 1, false, EndOfLine::LF};
                manager.documents().update(snapshot);
                promise.addResult(snapshot);
                promise.finish();
                return promise.future();
            },
            [&manager](const QString&, const UntitledTextDocumentOptions& options) {
                QPromise<TextDocumentSnapshot> promise;
                promise.start();
                TextDocumentSnapshot snapshot{"74", QUrl("untitled:Untitled-74"), options.language.value_or("plaintext"),
                    options.encoding.value_or("utf8"), options.content.value_or(QString()), 1, true, EndOfLine::LF};
                manager.documents().update(snapshot);
                promise.addResult(snapshot);
                promise.finish();
                return promise.future();
            }, [&removedOwners](const QString& owner) { removedOwners.append(owner); });
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.openDocument", {QUrl::fromLocalFile(directory.filePath("fixture.py")).toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        auto result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        QVERIFY(result.at(1).value<QJsonValue>().toObject().value("sameInstance").toBool());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("text").toString(), QString("fixture text"));
        QCOMPARE(opens, 1);
        QCOMPARE(openingOwner, QString("tests.native"));
        QVERIFY(manager.requestCommand("tests.native.openDocument", {QUrl::fromLocalFile(directory.filePath("fixture.py")).toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QVERIFY(result.at(1).value<QJsonValue>().toObject().value("sameInstance").toBool());
        QCOMPARE(opens, 1); // A live host cache hit does not renew workbench retention.
        QVERIFY(manager.requestCommand("tests.native.openDocument", {}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        const auto value = result.at(1).value<QJsonValue>().toObject();
        QVERIFY(value.value("sameInstance").toBool());
        QCOMPARE(value.value("language").toString(), QString("python"));
        QCOMPARE(QUrl(value.value("uri").toString()).scheme(), QString("untitled"));
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(removedOwners.contains("tests.native"));
    }
    void progressAcrossHostBoundary() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy started(&manager.progress(), &WorkbenchProgress::started);
        QSignalSpy changed(&manager.progress(), &WorkbenchProgress::changed);
        QSignalSpy ended(&manager.progress(), &WorkbenchProgress::ended);
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        const auto command = manager.requestCommand("tests.native.progress", {"complete"});
        QVERIFY(command != 0);
        QTRY_COMPARE(results.size(), 1);
        QTRY_COMPARE(ended.size(), 1);
        QCOMPARE(started.size(), 1);
        const auto first = started.takeFirst().first().value<ProgressTaskInfo>();
        QCOMPARE(first.owner, QStringLiteral("tests.native"));
        QCOMPARE(first.source, QStringLiteral("Native test fixture"));
        QCOMPARE(first.options.title.value(), QStringLiteral("Fixture progress"));
        QVERIFY(!manager.progress().task(first.owner, first.id));
        QVERIFY(!changed.isEmpty());
        QCOMPARE(changed.last().first().value<ProgressTaskInfo>().total.value(), 100.0);
        const auto completed = results.takeFirst();
        QCOMPARE(completed.at(0).toULongLong(), command);
        QVERIFY(completed.at(2).toString().isEmpty());
        QVERIFY(!completed.at(1).value<QJsonValue>().toObject().value("cancelled").toBool());
        changed.clear();
        ended.clear();
        QVERIFY(manager.requestCommand("tests.native.progress", {"pending"}) != 0);
        QTRY_COMPARE(started.size(), 1);
        const auto pending = started.takeFirst().first().value<ProgressTaskInfo>();
        QVERIFY(results.isEmpty());
        QVERIFY(manager.progress().cancel(pending.owner, pending.id));
        QTRY_COMPARE(results.size(), 1);
        QTRY_COMPARE(ended.size(), 1);
        const auto cancelled = results.takeFirst();
        QVERIFY(cancelled.at(2).toString().isEmpty());
        QVERIFY(cancelled.at(1).value<QJsonValue>().toObject().value("cancelled").toBool());
        QVERIFY(!manager.progress().task(pending.owner, pending.id));
        ended.clear();
        QVERIFY(manager.requestCommand("tests.native.progress", {"fail"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QTRY_COMPARE(ended.size(), 1);
        QVERIFY(results.takeFirst().at(2).toString().contains("Fixture progress failure"));
        started.clear();
        ended.clear();
        QVERIFY(manager.requestCommand("tests.native.progress", {"pending"}) != 0);
        QTRY_COMPARE(started.size(), 1);
        const auto disabled = started.takeFirst().first().value<ProgressTaskInfo>();
        QVERIFY(manager.setEnabled("tests.native", false));
        QCOMPARE(ended.size(), 1);
        QVERIFY(!manager.progress().task(disabled.owner, disabled.id));
        QVERIFY(!manager.progress().cancel(disabled.owner, disabled.id));
    }
    void fileDialogsAcrossHostBoundary() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy requests(&manager.dialogs(), &WorkbenchDialogs::requested);
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.fileDialog", {"open"}) != 0);
        QTRY_COMPARE(requests.size(), 1);
        auto request = requests.takeFirst().first().value<FileDialogRequest>();
        QCOMPARE(request.owner, QString("tests.native"));
        const auto& options = std::get<OpenDialogOptions>(request.options);
        QVERIFY(options.canSelectMany.value());
        QCOMPARE(options.filters->first().first, QString("Python"));
        QVERIFY(results.isEmpty());
        const QVector<QUrl> selected{QUrl::fromLocalFile(directory.filePath("one.py")), QUrl::fromLocalFile(directory.filePath("two.py"))};
        QVERIFY(manager.dialogs().complete(request.owner, request.id, selected));
        QTRY_COMPARE(results.size(), 1);
        const auto openResult = results.takeFirst();
        QVERIFY(openResult.at(2).toString().isEmpty());
        const auto values = openResult.at(1).value<QJsonValue>().toArray();
        QCOMPARE(values.size(), 2);
        QCOMPARE(QUrl(values.first().toString()), selected.first());
        QCOMPARE(QUrl(values.last().toString()), selected.last());
        QVERIFY(manager.requestCommand("tests.native.fileDialog", {"save"}) != 0);
        QTRY_COMPARE(requests.size(), 1);
        request = requests.takeFirst().first().value<FileDialogRequest>();
        QCOMPARE(std::get<SaveDialogOptions>(request.options).saveLabel.value(), QString("Fixture Save"));
        QVERIFY(manager.dialogs().complete(request.owner, request.id, {}));
        QTRY_COMPARE(results.size(), 1);
        const auto cancelled = results.takeFirst();
        QVERIFY(cancelled.at(2).toString().isEmpty());
        QVERIFY(cancelled.at(1).value<QJsonValue>().isUndefined());
        QVERIFY(manager.requestCommand("tests.native.fileDialog", {"save"}) != 0);
        QTRY_COMPARE(requests.size(), 1);
        request = requests.takeFirst().first().value<FileDialogRequest>();
        QSignalSpy dismissed(&manager.dialogs(), &WorkbenchDialogs::dismissed);
        QVERIFY(manager.setEnabled("tests.native", false));
        QCOMPARE(dismissed.size(), 1);
        QVERIFY(!manager.dialogs().complete(request.owner, request.id, {}));
    }
    void windowMessagesAcrossHostBoundary() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy requests(&manager.messages(), &WorkbenchMessages::requested);
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        const auto commandId = manager.requestCommand("tests.native.message", {true});
        QVERIFY(commandId != 0);
        QTRY_COMPARE(requests.size(), 1);
        const auto request = requests.takeFirst().first().value<WindowMessageRequest>();
        QCOMPARE(request.owner, QString("tests.native"));
        QCOMPARE(request.source, QString("Native test fixture"));
        QCOMPARE(request.severity, 2);
        QVERIFY(request.options.modal.value());
        QCOMPARE(request.options.detail.value(), QString("Fixture details"));
        QCOMPARE(request.items.size(), 2);
        QVERIFY(results.isEmpty());
        QVERIFY(manager.messages().select(request.owner, request.id, 1));
        QTRY_COMPARE(results.size(), 1);
        const auto selected = results.takeFirst();
        QCOMPARE(selected.at(0).toULongLong(), commandId);
        QVERIFY(selected.at(2).toString().isEmpty());
        QCOMPARE(selected.at(1).value<QJsonValue>().toObject().value("title").toString(), QString("Close"));
        QVERIFY(selected.at(1).value<QJsonValue>().toObject().value("close").toBool());
        QVERIFY(manager.requestCommand("tests.native.message", {false}) != 0);
        QTRY_COMPARE(requests.size(), 1);
        const auto nonmodal = requests.takeFirst().first().value<WindowMessageRequest>();
        QVERIFY(!nonmodal.options.modal.value());
        QVERIFY(manager.messages().select(nonmodal.owner, nonmodal.id, {}));
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(results.first().at(1).value<QJsonValue>().isUndefined());
        QVERIFY(results.takeFirst().at(2).toString().isEmpty());
        QVERIFY(manager.requestCommand("tests.native.message", {true}) != 0);
        QTRY_COMPARE(requests.size(), 1);
        const auto pending = requests.takeFirst().first().value<WindowMessageRequest>();
        QSignalSpy dismissed(&manager.messages(), &WorkbenchMessages::dismissed);
        QVERIFY(manager.setEnabled("tests.native", false));
        QCOMPARE(dismissed.size(), 1);
        QVERIFY(!manager.messages().select(pending.owner, pending.id, 0));
    }
    void outputChannelsAcrossHostBoundary() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QSignalSpy reveal(&manager.outputs(), &WorkbenchOutput::revealRequested);
        QVERIFY(manager.requestCommand("tests.native.output", {"create"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(results.takeFirst().at(2).toString().isEmpty());
        QTRY_COMPARE(manager.outputs().channels().size(), 2);
        QTRY_COMPARE(reveal.size(), 1);
        QVERIFY(reveal.first().at(1).toBool());
        QString outputId, logId;
        for (const auto& channel : manager.outputs().channels()) {
            QCOMPARE(channel.owner, QString("tests.native"));
            if (channel.log) logId = channel.id;
            else outputId = channel.id;
        }
        QVERIFY(!outputId.isEmpty());
        QVERIFY(!logId.isEmpty());
        QTRY_COMPARE(manager.outputs().channel(outputId)->text, QString("fixture output\n"));
        QTRY_VERIFY(manager.outputs().channel(logId)->text.contains("fixture info"));
        QVERIFY(!manager.outputs().channel(logId)->text.contains("filtered trace"));
        manager.outputs().setLogLevel(logId, LogLevel::Error);
        QVERIFY(manager.requestCommand("tests.native.output", {"level"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        const auto levelResult = results.takeFirst();
        QVERIFY(levelResult.at(2).toString().isEmpty());
        QCOMPARE(levelResult.at(1).value<QJsonValue>().toObject().value("level").toInt(), static_cast<int>(LogLevel::Error));
        QCOMPARE(levelResult.at(1).value<QJsonValue>().toObject().value("eventLevel").toInt(), static_cast<int>(LogLevel::Error));
        manager.outputs().clear(logId);
        QVERIFY(manager.requestCommand("tests.native.output", {"error"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(results.takeFirst().at(2).toString().isEmpty());
        QTRY_VERIFY(manager.outputs().channel(logId)->text.contains("fixture error"));
        QVERIFY(!manager.outputs().channel(logId)->text.contains("fixture info"));
        QVERIFY(!manager.outputs().channel(logId)->text.contains("filtered warning"));
        QVERIFY(manager.requestCommand("tests.native.output", {"dispose"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(results.takeFirst().at(2).toString().isEmpty());
        QTRY_VERIFY(manager.outputs().channels().isEmpty());
        QVERIFY(manager.requestCommand("tests.native.output", {"closed"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        const auto closedResult = results.takeFirst();
        QVERIFY(closedResult.at(2).toString().isEmpty());
        QVERIFY(closedResult.at(1).value<QJsonValue>().toBool());
        QVERIFY(manager.requestCommand("tests.native.output", {"create"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QTRY_COMPARE(manager.outputs().channels().size(), 2);
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(manager.outputs().channels().isEmpty());
    }
    void disposableOwnership() {
        int disposals = 0;
        {
            Disposable first([&] { ++disposals; });
            Disposable second(std::move(first));
            QVERIFY(!first);
            QVERIFY(second);
            second.dispose();
            second.dispose();
            QCOMPARE(disposals, 1);
            second = Disposable([&] { ++disposals; });
            second = Disposable([&] { ++disposals; });
            QCOMPARE(disposals, 1);
        }
        QCOMPARE(disposals, 1);
        Disposable retained;
        {
            Disposable temporary([&] { ++disposals; });
            retained = std::move(temporary);
        }
        QCOMPARE(disposals, 1);
        retained.dispose();
        QCOMPARE(disposals, 2);
    }
    void manifestValidation() {
        QJsonObject manifest{{"id", "tests.example"},
                             {"displayName", "Example"},
                             {"version", "1.0.0"},
                             {"apiVersion", ApiVersion},
                             {"qtVersion", qVersion()},
                             {"architecture", QSysInfo::buildCpuArchitecture()},
                             {"activationEvents", QJsonArray{"onCommand:tests.example.hello"}}};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        // SDK 14 native controls have an incompatible C++ layout. Refuse them
        // before activation rather than letting a stale installed DLL crash.
        manifest["apiVersion"] = 14;
        const auto staleSdk = ExtensionManager::validateManifest(manifest);
        QVERIFY(staleSdk.contains("SDK 14"));
        QVERIFY(staleSdk.contains(QString("SDK %1").arg(ApiVersion)));
        manifest["apiVersion"] = 999;
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["apiVersion"] = ApiVersion;
        manifest["activationEvents"] = QJsonArray{"onCommand:someone.else.hello"};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["activationEvents"] = QJsonArray{"onLanguage:python"};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["activationEvents"] = QJsonArray{"onChatParticipant:tests.example.chat"};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["activationEvents"] = QJsonArray{"onLanguage:"};
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
        manifest.remove("activationEvents");
        const QJsonObject command{
            {"command", "tests.example.hello"}, {"title", "Hello"}, {"category", "Example"}};
        manifest["contributes"] = QJsonObject{{"commands", QJsonArray{command}}};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"commands", QJsonArray{
            QJsonObject{{"command", "unprefixed-command"}, {"title", "Other"}}}}};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"formatters", QJsonArray{
            QJsonObject{{"language", "python"}}}}};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"formatters", QJsonArray{
            QJsonObject{{"language", "python"}}, QJsonObject{{"language", "python"}}}}};
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
        const QJsonObject debugger{{"type", "fixture"},
                                   {"configurationAttributes", QJsonObject{
                                        {"launch", QJsonObject{{"properties", QJsonObject{
                                             {"interpreter", QJsonObject{{"type", "string"}}}}}}}}}};
        manifest["contributes"] = QJsonObject{{"debuggers", QJsonArray{debugger}}};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"debuggers", QJsonArray{debugger, debugger}}};
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"commands", QJsonArray{command, command}}};
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"commands", "not an array"}};
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
        const QJsonObject participant{{"id", "tests.example.chat"},
                                      {"name", "my-chat"},
                                      {"fullName", "My Chat"}};
        manifest["contributes"] = QJsonObject{{"chatParticipants", QJsonArray{participant}}};
        QVERIFY(ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"chatParticipants",
            QJsonArray{participant, participant}}};
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
        manifest["contributes"] = QJsonObject{{"chatParticipants", QJsonArray{
            QJsonObject{{"id", "tests.example.chat"}, {"name", "invalid name"}}}}};
        QVERIFY(!ExtensionManager::validateManifest(manifest).isEmpty());
    }
    void emptyDiscovery() {
        QTemporaryDir directory;
        ExtensionManager manager(directory.path());
        manager.discover();
        QVERIFY(manager.extensions().isEmpty());
        QVERIFY(!manager.setEnabled("missing", true));
        QVERIFY(!manager.executeCommand("missing"));
    }
    void contributedCommandCollisionRecoversAfterDisable() {
        QTemporaryDir directory;
        const QString first = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        const QString second = directory.filePath(
            QFileInfo(LITECODE_CONFLICTING_COMMAND_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, first));
        QVERIFY(QFile::copy(LITECODE_CONFLICTING_COMMAND_TEST_PLUGIN, second));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QCOMPARE(manager.extensions().size(), 2);
        QVERIFY(manager.setEnabled("tests.native", true));
        QVERIFY(!manager.setEnabled("tests.commandConflict", true));
        QVERIFY(manager.commands().contains("tests.native.hello"));
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(manager.setEnabled("tests.commandConflict", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        const auto requestId = manager.requestCommand("tests.native.hello");
        QVERIFY(requestId != 0);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.first().at(0).toULongLong(), requestId);
        QCOMPARE(results.first().at(1).toString(), QString("conflicting extension"));
        QVERIFY(results.first().at(2).toString().isEmpty());
    }
    void chatParticipantCollisionRecoversAfterDisable() {
        QTemporaryDir directory;
        const QString first = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        const QString second = directory.filePath(
            QFileInfo(LITECODE_CONFLICTING_CHAT_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, first));
        QVERIFY(QFile::copy(LITECODE_CONFLICTING_CHAT_TEST_PLUGIN, second));
        ExtensionManager manager(directory.path());
        manager.discover();
        QCOMPARE(manager.extensions().size(), 2);
        QVERIFY(manager.setEnabled("tests.native", true));
        QVERIFY(!manager.setEnabled("tests.chatConflict", true));
        QCOMPARE(manager.chatParticipantContributions().size(), 1);
        QCOMPARE(manager.chatParticipantContributions().first().extensionId,
                 QString("tests.native"));
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(manager.setEnabled("tests.chatConflict", true));
        QCOMPARE(manager.chatParticipantContributions().size(), 1);
        QCOMPARE(manager.chatParticipantContributions().first().extensionId,
                 QString("tests.chatConflict"));
    }
    void chatParticipantActivationIsLazyAndOwned() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(!manager.activateForChatParticipant("tests.native.assistant"));
        QVERIFY(manager.setEnabled("tests.native", true));
        QVERIFY(!manager.extensions().first().active);
        QVERIFY(!manager.activateForChatParticipant("tests.native.missing"));
        QVERIFY(!manager.extensions().first().active);
        QVERIFY(manager.activateForChatParticipant("tests.native.assistant"));
        QTRY_VERIFY(manager.extensions().first().active);
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(!manager.activateForChatParticipant("tests.native.assistant"));
        QVERIFY(!manager.extensions().first().active);
    }
    void chatRequestsStreamHistoryAndCancellationAcrossHost() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy parts(&manager, &ExtensionManager::chatResponsePart);
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        QSignalSpy followups(&manager, &ExtensionManager::chatFollowupsReady);
        QSignalSpy messages(&manager, &ExtensionManager::informationMessage);
        QCOMPARE(manager.requestChat("session", "tests.native.missing",
                                     {{"prompt", "hello"}}), quint64(0));
        const auto first = manager.requestChat("session", "tests.native.assistant",
            {{"prompt", "first"}, {"command", "explain"},
             {"references", QJsonArray{QJsonObject{{"id", "file"},
                                                    {"value", "fixture.txt"}}}}});
        QVERIFY(first != 0);
        QTRY_VERIFY([&] {
            return std::any_of(finished.cbegin(), finished.cend(), [first](const auto& item) {
                return item.at(0).toULongLong() == first;
            });
        }());
        QCOMPARE(parts.size(), 6);
        for (qsizetype index = 0; index < parts.size(); ++index) {
            QCOMPARE(parts.at(index).at(0).toULongLong(), first);
            QCOMPARE(parts.at(index).at(1).toULongLong(), quint64(index));
        }
        QCOMPARE(parts.at(0).at(2).value<QJsonObject>().value("kind").toString(),
                 QString("progress"));
        QCOMPARE(parts.at(1).at(2).value<QJsonObject>().value("value").toObject()
                     .value("value").toString(), QString("history=0 prompt=first"));
        QCOMPARE(parts.at(2).at(2).value<QJsonObject>().value("kind").toString(),
                 QString("anchor"));
        QCOMPARE(parts.at(3).at(2).value<QJsonObject>().value("kind").toString(),
                 QString("reference"));
        QCOMPARE(parts.at(4).at(2).value<QJsonObject>().value("kind").toString(),
                 QString("button"));
        QCOMPARE(parts.at(5).at(2).value<QJsonObject>().value("kind").toString(),
                 QString("filetree"));
        QCOMPARE(finished.first().at(2).toString(), QString());
        const auto firstResult = finished.first().at(1).value<QJsonObject>();
        QCOMPARE(firstResult.value("metadata").toObject().value("command").toString(),
                 QString("explain"));
        QCOMPARE(firstResult.value("metadata").toObject().value("references").toInt(), 1);
        QTRY_COMPARE(followups.size(), 1);
        QCOMPARE(followups.first().at(0).toULongLong(), first);
        QVERIFY(followups.first().at(2).toString().isEmpty());
        const auto firstFollowup = followups.first().at(1).value<QJsonArray>().first().toObject();
        QCOMPARE(firstFollowup.value("prompt").toString(), QString("Ask another question"));
        QCOMPARE(firstFollowup.value("label").toString(), QString("Ask again"));
        QCOMPARE(firstFollowup.value("participant").toString(),
                 QString("tests.native.assistant"));
        QCOMPARE(firstFollowup.value("command").toString(), QString("explain"));
        QVERIFY(manager.sendChatFeedback(first, ChatResultFeedbackKind::Helpful));
        QTRY_VERIFY([&] {
            return std::any_of(messages.cbegin(), messages.cend(), [](const auto& item) {
                return item.at(0).toString() == "Chat feedback 1 command=explain";
            });
        }());
        QVERIFY(!manager.sendChatFeedback(first,
            static_cast<ChatResultFeedbackKind>(17)));
        const auto history = manager.chatHistory("session", "tests.native.assistant");
        QCOMPARE(history.size(), 2);
        QCOMPARE(history.at(1).toObject().value("response").toArray().size(), 4);
        QCOMPARE(history.at(1).toObject().value("result").toObject(), firstResult);
        QVERIFY(manager.chatHistory("other", "tests.native.assistant").isEmpty());

        const auto second = manager.requestChat("session", "tests.native.assistant",
                                                {{"prompt", "async"}});
        QVERIFY(second > first);
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(0).toULongLong(), second);
        QCOMPARE(finished.at(1).at(2).toString(), QString());
        QCOMPARE(finished.at(1).at(1).value<QJsonObject>()
                     .value("metadata").toObject().value("async").toBool(), true);
        QTRY_COMPARE(followups.size(), 2);
        QCOMPARE(followups.at(1).at(0).toULongLong(), second);
        QVERIFY(followups.at(1).at(2).toString().isEmpty());
        QCOMPARE(manager.chatHistory("session", "tests.native.assistant").size(), 4);
        QVERIFY(std::any_of(parts.cbegin(), parts.cend(), [second](const auto& item) {
            return item.at(0).toULongLong() == second &&
                   item.at(2).template value<QJsonObject>().value("value").toObject()
                       .value("value").toString()
                       .startsWith("history=2");
        }));

        const auto cancelled = manager.requestChat("session", "tests.native.assistant",
                                                   {{"prompt", "cancel"}});
        QVERIFY(cancelled > second);
        QTRY_VERIFY([&] {
            return std::any_of(parts.cbegin(), parts.cend(), [cancelled](const auto& item) {
                return item.at(0).toULongLong() == cancelled;
            });
        }());
        QVERIFY(manager.cancelChat(cancelled));
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(finished.at(2).at(0).toULongLong(), cancelled);
        QCOMPARE(finished.at(2).at(2).toString(), QString("Cancelled."));
        QVERIFY(!manager.cancelChat(cancelled));
        QTRY_VERIFY([&] {
            return std::any_of(messages.cbegin(), messages.cend(), [](const auto& item) {
                return item.at(0).toString() == "Chat cancellation observed";
            });
        }());
        QTest::qWait(350);
        QCOMPARE(finished.size(), 3);
        QCOMPARE(manager.chatHistory("session", "tests.native.assistant").size(), 4);

        const auto failure = manager.requestChat("session", "tests.native.assistant",
                                                 {{"prompt", "throw"}});
        QVERIFY(failure > cancelled);
        QTRY_COMPARE(finished.size(), 4);
        QCOMPARE(finished.at(3).at(0).toULongLong(), failure);
        QCOMPARE(finished.at(3).at(2).toString(), QString("chat fixture failure"));
        const auto partsBeforeLate = parts.size();
        const auto late = manager.requestChat("session", "tests.native.assistant",
                                              {{"prompt", "late"}});
        QVERIFY(late > failure);
        QTRY_COMPARE(finished.size(), 5);
        QTRY_VERIFY([&] {
            return std::any_of(messages.cbegin(), messages.cend(), [](const auto& item) {
                return item.at(0).toString() == "Late chat write rejected";
            });
        }());
        QCOMPARE(parts.size(), partsBeforeLate + 2);
        const auto foreign = manager.requestChat("session", "tests.native.assistant",
                                                 {{"prompt", "foreignFollowup"}});
        QVERIFY(foreign > late);
        QTRY_COMPARE(finished.size(), 6);
        QTRY_VERIFY([&] {
            return std::any_of(followups.cbegin(), followups.cend(),
                               [foreign](const auto& item) {
                return item.at(0).toULongLong() == foreign;
            });
        }());
        const auto rejectedFollowup = std::find_if(
            followups.cbegin(), followups.cend(), [foreign](const auto& item) {
                return item.at(0).toULongLong() == foreign;
            });
        QVERIFY(rejectedFollowup != followups.cend());
        QVERIFY(rejectedFollowup->at(1).value<QJsonArray>().isEmpty());
        QVERIFY(rejectedFollowup->at(2).toString().contains("same extension"));

        const auto slow = manager.requestChat("session", "tests.native.assistant",
                                              {{"prompt", "slowFollowup"}});
        QVERIFY(slow > foreign);
        QTRY_COMPARE(finished.size(), 7);
        manager.closeChatSession("session");
        QVERIFY(manager.chatHistory("session", "tests.native.assistant").isEmpty());
        QVERIFY(!manager.sendChatFeedback(first, ChatResultFeedbackKind::Unhelpful));
        QTest::qWait(350);
        QVERIFY(std::none_of(followups.cbegin(), followups.cend(), [slow](const auto& item) {
            return item.at(0).toULongLong() == slow;
        }));
    }
    void typedChatResponsePartsCrossHost() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy parts(&manager, &ExtensionManager::chatResponsePart);
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        const auto id = manager.requestChat("session", "tests.native.assistant",
                                            {{"prompt", "typed"}});
        QVERIFY(id != 0);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(2).toString(), QString());
        QCOMPARE(parts.size(), 7);
        const auto markdown = parts.at(2).at(2).value<QJsonObject>();
        QCOMPARE(markdown.value("kind").toString(), QString("markdown"));
        const auto markdownValue = markdown.value("value").toObject();
        QCOMPARE(markdownValue.value("value").toString(), QString("[source](./fixture.txt)"));
        QCOMPARE(markdownValue.value("baseUri").toString(), QString("file:///workspace/"));
        QVERIFY(markdownValue.value("supportThemeIcons").toBool());
        QCOMPARE(markdownValue.value("isTrusted").toObject()
                     .value("enabledCommands").toArray().first().toString(),
                 QString("tests.native.hello"));
        const auto anchor = parts.at(3).at(2).value<QJsonObject>();
        QCOMPARE(anchor.value("title").toString(), QString("Located"));
        const auto location = anchor.value("value").toObject();
        QCOMPARE(location.value("uri").toString(), QString("file:///fixture.txt"));
        QCOMPARE(location.value("range").toObject().value("start").toObject()
                     .value("line").toInt(), 2);
        QCOMPARE(location.value("range").toObject().value("end").toObject()
                     .value("character").toInt(), 9);
        const auto button = parts.at(4).at(2).value<QJsonObject>().value("value").toObject();
        QCOMPARE(button.value("command").toString(), QString("tests.native.hello"));
        QCOMPARE(button.value("tooltip").toString(), QString("Run hello"));
        QCOMPARE(button.value("arguments").toArray().first().toInt(), 42);
        const auto tree = parts.at(5).at(2).value<QJsonObject>().value("value").toArray();
        QCOMPARE(tree.first().toObject().value("name").toString(), QString("folder"));
        const auto child = tree.first().toObject().value("children").toArray().first().toObject();
        QCOMPARE(child.value("name").toString(), QString("empty"));
        QVERIFY(child.value("children").isArray());
        QVERIFY(child.value("children").toArray().isEmpty());
        const auto reference = parts.at(6).at(2).value<QJsonObject>();
        QCOMPARE(reference.value("kind").toString(), QString("reference"));
        QCOMPARE(reference.value("iconPath").toObject().value("id").toString(),
                 QString("file"));
        QCOMPARE(reference.value("iconPath").toObject().value("color").toObject()
                     .value("id").toString(), QString("charts.blue"));
        const auto history = manager.chatHistory("session", "tests.native.assistant");
        QCOMPARE(history.size(), 2);
        QCOMPARE(history.at(1).toObject().value("response").toArray().size(), 5);
    }
    void chatPromptReferencesCrossHostInReverseRangeOrder() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        const QJsonObject sourceRange{
            {"start", QJsonObject{{"line", 2}, {"character", 1}}},
            {"end", QJsonObject{{"line", 2}, {"character", 4}}}};
        const QJsonObject locationValue{{"uri", "file:///b.py"},
                                        {"range", sourceRange}};
        const QJsonArray references{
            QJsonObject{{"id", "a"}, {"range", QJsonArray{0, 2}},
                        {"$litecodeValueType", "uri"}, {"value", "file:///a.py"}},
            QJsonObject{{"id", "attachment"}, {"value", QJsonObject{{"data", 7}}}},
            QJsonObject{{"id", "b"}, {"range", QJsonArray{3, 5}},
                        {"$litecodeValueType", "location"}, {"value", locationValue}}};
        const QJsonArray toolReferences{
            QJsonObject{{"name", "first"}, {"range", QJsonArray{0, 2}}},
            QJsonObject{{"name", "second"}, {"range", QJsonArray{3, 5}}}};
        const QJsonObject request{{"prompt", "#a #b"},
                                  {"references", references},
                                  {"toolReferences", toolReferences}};
        const auto id = manager.requestChat("session", "tests.native.assistant", request);
        QVERIFY(id != 0);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(2).toString(), QString());
        const auto metadata = finished.first().at(1).value<QJsonObject>()
                                  .value("metadata").toObject();
        QCOMPARE(metadata.value("referenceIds").toArray(),
                 QJsonArray({"b", "a", "attachment"}));
        QCOMPARE(metadata.value("toolNames").toArray(), QJsonArray({"second", "first"}));
        QCOMPARE(metadata.value("firstRangeStart").toInt(), 3);
        QVERIFY(metadata.value("firstIsLocation").toBool());
        QVERIFY(metadata.value("secondIsUri").toBool());
        QCOMPARE(metadata.value("attachedValue").toObject().value("data").toInt(), 7);
        const auto history = manager.chatHistory("session", "tests.native.assistant");
        QCOMPARE(history.size(), 2);
        QCOMPARE(history.first().toObject().value("references").toArray().first()
                     .toObject().value("id").toString(), QString("b"));
        QJsonObject invalid = request;
        invalid.insert("references", QJsonArray{QJsonObject{{"id", "bad"},
            {"range", QJsonArray{0, 6}}, {"value", "outside"}}});
        QCOMPARE(manager.requestChat("session", "tests.native.assistant", invalid),
                 quint64(0));
        QCOMPARE(manager.chatHistory("session", "tests.native.assistant").size(), 2);
    }
    void typedChatHistoryCrossesHostWithContentAndResult() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        const auto first = manager.requestChat("session", "tests.native.assistant",
            {{"prompt", "first"}, {"command", "explain"},
             {"references", QJsonArray{QJsonObject{{"id", "file"},
                                                    {"value", "fixture.txt"}}}}});
        QVERIFY(first != 0);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(2).toString(), QString());
        const auto second = manager.requestChat("session", "tests.native.assistant",
                                                {{"prompt", "history-check"}});
        QVERIFY(second > first);
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(2).toString(), QString());
        const auto metadata = finished.at(1).at(1).value<QJsonObject>()
                                  .value("metadata").toObject();
        QVERIFY(metadata.value("requestTyped").toBool());
        QVERIFY(metadata.value("responseTyped").toBool());
        QCOMPARE(metadata.value("firstPrompt").toString(), QString("first"));
        QCOMPARE(metadata.value("firstCommand").toString(), QString("explain"));
        QCOMPARE(metadata.value("firstReference").toString(), QString("file"));
        QCOMPARE(metadata.value("contentCount").toInt(), 4);
        QCOMPARE(metadata.value("firstMarkdown").toString(),
                 QString("history=0 prompt=first"));
        QCOMPARE(metadata.value("firstResultCommand").toString(), QString("explain"));
        QCOMPARE(manager.chatHistory("session", "tests.native.assistant").size(), 4);
    }
    void chatRequestFailsOnceOnDisableAndHostExit() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        const auto pending = manager.requestChat("session", "tests.native.assistant",
                                                 {{"prompt", "cancel"}});
        QVERIFY(pending != 0);
        QVERIFY(manager.setEnabled("tests.native", false));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toULongLong(), pending);
        QCOMPARE(finished.first().at(2).toString(), QString("Extension disabled."));
        QVERIFY(manager.setEnabled("tests.native", true));
        const auto inFlight = manager.requestChat("session", "tests.native.assistant",
                                                  {{"prompt", "cancel"}});
        QVERIFY(inFlight > pending);
        QSignalSpy parts(&manager, &ExtensionManager::chatResponsePart);
        QTRY_VERIFY([&] {
            return std::any_of(parts.cbegin(), parts.cend(), [inFlight](const auto& item) {
                return item.at(0).toULongLong() == inFlight;
            });
        }());
        QVERIFY(manager.executeCommand("tests.native.exitHost"));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(0).toULongLong(), inFlight);
        QVERIFY(!finished.at(1).at(2).toString().isEmpty());
        QTest::qWait(350);
        QCOMPARE(finished.size(), 2);
        QVERIFY(manager.chatHistory("session", "tests.native.assistant").isEmpty());
    }
    void disposingChatParticipantEndsInFlightRequestAndFeedback() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        QSignalSpy parts(&manager, &ExtensionManager::chatResponsePart);
        const auto completed = manager.requestChat("session", "tests.native.assistant",
                                                    {{"prompt", "first"}});
        QVERIFY(completed != 0);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(manager.sendChatFeedback(completed, ChatResultFeedbackKind::Helpful));
        const auto active = manager.requestChat("session", "tests.native.assistant",
                                                 {{"prompt", "cancel"}});
        QVERIFY(active > completed);
        QTRY_VERIFY([&] {
            return std::any_of(parts.cbegin(), parts.cend(), [active](const auto& item) {
                return item.at(0).toULongLong() == active;
            });
        }());
        QVERIFY(manager.executeCommand("tests.native.disposeParticipant"));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(0).toULongLong(), active);
        QCOMPARE(finished.at(1).at(2).toString(),
                 QString("Chat participant was disposed."));
        QVERIFY(!manager.sendChatFeedback(completed, ChatResultFeedbackKind::Helpful));
        QTest::qWait(350);
        QCOMPARE(finished.size(), 2);
        QVERIFY(manager.extensions().first().active);
    }
    void hostExitFinishesPendingFollowupOnce() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        QSignalSpy followups(&manager, &ExtensionManager::chatFollowupsReady);
        const auto id = manager.requestChat("session", "tests.native.assistant",
                                            {{"prompt", "slowFollowup"}});
        QVERIFY(id != 0);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.first().at(2).toString().isEmpty());
        QVERIFY(manager.executeCommand("tests.native.exitHost"));
        QTRY_COMPARE(followups.size(), 1);
        QCOMPARE(followups.first().at(0).toULongLong(), id);
        QVERIFY(!followups.first().at(2).toString().isEmpty());
        QTest::qWait(1100);
        QCOMPARE(followups.size(), 1);
        QVERIFY(!manager.sendChatFeedback(id, ChatResultFeedbackKind::Helpful));
    }
    void chatParticipantHandlerAndIconCanChange() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy parts(&manager, &ExtensionManager::chatResponsePart);
        QSignalSpy finished(&manager, &ExtensionManager::chatRequestFinished);
        QSignalSpy followups(&manager, &ExtensionManager::chatFollowupsReady);
        const auto first = manager.requestChat("session", "tests.native.assistant",
                                                {{"prompt", "before"}});
        QVERIFY(first != 0);
        QTRY_COMPARE(finished.size(), 1);
        QTRY_VERIFY(manager.chatParticipantContributions().first().iconPath.isObject());
        QCOMPARE(manager.chatParticipantContributions().first().iconPath.toObject()
                     .value("dark").toString(), QString("file:///fixture-dark.svg"));
        QVERIFY(manager.executeCommand("tests.native.replaceParticipantHandler"));
        QTRY_COMPARE(manager.chatParticipantContributions().first().iconPath.toString(),
                     QString("file:///replacement.svg"));
        QVERIFY(manager.executeCommand("tests.native.themeParticipantIcon"));
        QTRY_COMPARE(manager.chatParticipantContributions().first().iconPath.toObject()
                         .value("id").toString(), QString("chat"));
        const auto second = manager.requestChat("session", "tests.native.assistant",
                                                 {{"prompt", "after"}});
        QVERIFY(second > first);
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(1).at(1).value<QJsonObject>()
                     .value("metadata").toObject().value("replaced").toBool(), true);
        QVERIFY(std::any_of(parts.cbegin(), parts.cend(), [second](const auto& item) {
            return item.at(0).toULongLong() == second &&
                   item.at(2).template value<QJsonObject>().value("value").toObject()
                       .value("value").toString() ==
                       "replacement:after";
        }));
        QSignalSpy commandResults(&manager, &ExtensionManager::commandResult);
        const auto clearId = manager.requestCommand("tests.native.clearFollowupProvider");
        QVERIFY(clearId != 0);
        QTRY_VERIFY([&] {
            return std::any_of(commandResults.cbegin(), commandResults.cend(),
                               [clearId](const auto& item) {
                return item.at(0).toULongLong() == clearId;
            });
        }());
        const auto third = manager.requestChat("session", "tests.native.assistant",
                                               {{"prompt", "no-followups"}});
        QVERIFY(third > second);
        QTRY_COMPARE(finished.size(), 3);
        QTRY_VERIFY([&] {
            return std::any_of(followups.cbegin(), followups.cend(), [third](const auto& item) {
                return item.at(0).toULongLong() == third;
            });
        }());
        const auto emptyFollowups = std::find_if(
            followups.cbegin(), followups.cend(), [third](const auto& item) {
                return item.at(0).toULongLong() == third;
            });
        QVERIFY(emptyFollowups != followups.cend());
        QVERIFY(emptyFollowups->at(1).value<QJsonArray>().isEmpty());
        QVERIFY(emptyFollowups->at(2).toString().isEmpty());
    }
    void commandServiceRoutesAcrossSeparateHosts() {
        QTemporaryDir directory;
        const QString first = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        const QString second = directory.filePath(
            QFileInfo(LITECODE_OTHER_FORMATTER_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, first));
        QVERIFY(QFile::copy(LITECODE_OTHER_FORMATTER_TEST_PLUGIN, second));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QVERIFY(manager.setEnabled("tests.otherFormatter", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        const auto bridgeId = manager.requestCommand("tests.native.bridge", {"cross-host"});
        QVERIFY(bridgeId != 0);
        QTRY_VERIFY([&] {
            for (const auto& item : results)
                if (item.at(0).toULongLong() == bridgeId) return true;
            return false;
        }());
        for (const auto& item : results) {
            if (item.at(0).toULongLong() != bridgeId) continue;
            QCOMPARE(item.at(1).value<QJsonValue>(), QJsonValue("cross-host"));
            QVERIFY(item.at(2).toString().isEmpty());
        }
        const auto listId = manager.requestCommand("tests.native.commandList");
        QVERIFY(listId != 0);
        QTRY_VERIFY([&] {
            for (const auto& item : results)
                if (item.at(0).toULongLong() == listId) return true;
            return false;
        }());
        for (const auto& item : results) {
            if (item.at(0).toULongLong() != listId) continue;
            const auto values = item.at(1).value<QJsonValue>().toArray();
            QVERIFY(values.contains("tests.native.bridge"));
            QVERIFY(values.contains("tests.otherFormatter.echo"));
            QVERIFY(!values.contains("_tests.native.internal"));
            QVERIFY(item.at(2).toString().isEmpty());
        }
        const auto missingId = manager.requestCommand("tests.native.missingCommand");
        QVERIFY(missingId != 0);
        QTRY_VERIFY([&] {
            for (const auto& item : results)
                if (item.at(0).toULongLong() == missingId) return true;
            return false;
        }());
        for (const auto& item : results)
            if (item.at(0).toULongLong() == missingId)
                QVERIFY(item.at(2).toString().contains("Command not found"));
    }
    void commandServiceUsesWorkbenchRegistry() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        QString invoked;
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkbenchCommandSource(
            [] { return QStringList{"workbench.fixture", "_workbench.hidden"}; },
            [&invoked](const QString& id) { invoked = id; return true; });
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        const auto invokeId = manager.requestCommand("tests.native.invokeWorkbench");
        QVERIFY(invokeId != 0);
        // A forwarded workbench command has its own result before the outer
        // extension invocation finishes. Match correlation IDs, not signal order.
        const auto resultFor = [&results](quint64 id) {
            const auto found = std::find_if(results.cbegin(), results.cend(),
                [id](const QList<QVariant>& item) { return item.at(0).toULongLong() == id; });
            return found == results.cend() ? QList<QVariant>{} : *found;
        };
        QTRY_VERIFY(!resultFor(invokeId).isEmpty());
        QCOMPARE(invoked, QString("workbench.fixture"));
        QVERIFY(resultFor(invokeId).at(1).value<QJsonValue>().isUndefined());
        QVERIFY(resultFor(invokeId).at(2).toString().isEmpty());

        const auto filteredId = manager.requestCommand("tests.native.commandList");
        QVERIFY(filteredId != 0);
        QTRY_VERIFY(!resultFor(filteredId).isEmpty());
        QVERIFY(resultFor(filteredId).at(2).toString().isEmpty());
        const auto filtered = resultFor(filteredId).at(1).value<QJsonValue>().toArray();
        QVERIFY(filtered.contains("workbench.fixture"));
        QVERIFY(!filtered.contains("_workbench.hidden"));

        const auto allId = manager.requestCommand("tests.native.commandListAll");
        QVERIFY(allId != 0);
        QTRY_VERIFY(!resultFor(allId).isEmpty());
        QVERIFY(resultFor(allId).at(2).toString().isEmpty());
        QVERIFY(resultFor(allId).at(1).value<QJsonValue>().toArray()
                    .contains("_workbench.hidden"));

        const auto argsId = manager.requestCommand("tests.native.invokeWorkbenchWithArgs");
        QVERIFY(argsId != 0);
        QTRY_VERIFY(!resultFor(argsId).isEmpty());
        QVERIFY(resultFor(argsId).at(2).toString().contains("arguments"));
        QCOMPARE(invoked, QString("workbench.fixture"));
    }
    void extensionCannotClaimWorkbenchCommandId() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        {
            ExtensionManager manager(directory.path());
            manager.setWorkbenchCommandSource(
                [] { return QStringList{"tests.native.hello"}; },
                [](const QString&) { return true; });
            manager.discover();
            QVERIFY(!manager.setEnabled("tests.native", true));
            QVERIFY(manager.extensions().first().error.contains(
                "Duplicate contributed command ID"));
        }
        // Even an extension approved before the workbench registered this ID
        // must not retain command ownership on the next discovery.
        {
            ExtensionManager manager(directory.path());
            manager.discover();
            QVERIFY(manager.setEnabled("tests.native", true));
        }
        ExtensionManager restarted(directory.path());
        restarted.setWorkbenchCommandSource(
            [] { return QStringList{"tests.native.hello"}; },
            [](const QString&) { return true; });
        restarted.discover();
        QVERIFY(!restarted.extensions().first().enabled);
        QVERIFY(restarted.extensions().first().error.contains(
            "Duplicate contributed command ID"));
    }
    void queuesCommandsDuringActivation() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        const auto first = manager.requestCommand("tests.native.hello");
        const auto second = manager.requestCommand("tests.native.hello");
        QVERIFY(first != 0);
        QVERIFY(second > first);
        QTRY_COMPARE(results.size(), 2);
        QCOMPARE(results.at(0).at(0).toULongLong(), first);
        QCOMPARE(results.at(1).at(0).toULongLong(), second);
        QVERIFY(results.at(0).at(2).toString().isEmpty());
        QVERIFY(results.at(1).at(2).toString().isEmpty());
    }
    void nativeLifecycle() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        {
            ExtensionManager manager(directory.path());
            manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
            manager.setWorkspace(directory.path());
            manager.discover();
            QCOMPARE(manager.extensions().size(), 1);
            QVERIFY2(manager.extensions()[0].error.isEmpty(),
                     qPrintable(manager.extensions()[0].error));
            QVERIFY(!manager.extensions()[0].enabled);
            QVERIFY(!manager.executeCommand("tests.native.hello"));
            QVERIFY(manager.setEnabled("tests.native", true));
            QVERIFY(!manager.extensions()[0].active);
            QSignalSpy formats(&manager, &ExtensionManager::documentFormatResult);
            const auto formatId = manager.requestDocumentFormat("python", "hello  ");
            QVERIFY(formatId != 0);
            QTRY_COMPARE(formats.size(), 1);
            QCOMPARE(formats.at(0).at(0).toULongLong(), formatId);
            QCOMPARE(formats.at(0).at(1).toString(), QString("hello\n"));
            QCOMPARE(formats.at(0).at(2).toString(), QString());
            const auto secondFormatId = manager.requestDocumentFormat("python", "again  ");
            QVERIFY(secondFormatId != 0);
            QTRY_COMPARE(formats.size(), 2);
            QCOMPARE(formats.at(1).at(0).toULongLong(), secondFormatId);
            QCOMPARE(formats.at(1).at(1).toString(), QString("again\n"));
            QCOMPARE(manager.requestDocumentFormat("unknown-language", "hello"), quint64(0));
            QSignalSpy adapters(&manager, &ExtensionManager::debugAdapterResult);
            const auto adapterId = manager.requestDebugAdapter(
                "fixture", QJsonObject{{"adapterForTest", "fixture-adapter"}});
            QVERIFY(adapterId != 0);
            QTRY_COMPARE(adapters.size(), 1);
            QCOMPARE(adapters.at(0).at(0).toULongLong(), adapterId);
            QCOMPARE(adapters.at(0).at(1).value<QJsonObject>().value("command").toString(), QString("fixture-adapter"));
            QCOMPARE(adapters.at(0).at(1).value<QJsonObject>().value("args").toArray(), QJsonArray{"--fixture"});
            QCOMPARE(adapters.at(0).at(2).toString(), QString());
            QCOMPARE(manager.requestDebugAdapter("unknown", {}), quint64(0));
            const auto contributions = manager.commandContributions();
            QVERIFY(std::any_of(contributions.begin(), contributions.end(),
                                [](const auto& command) {
                                    return command.id == "tests.native.hello";
                                }));
            const auto debuggers = manager.debuggerContributions();
            QCOMPARE(debuggers.size(), 1);
            QCOMPARE(debuggers.first().type, QStringLiteral("fixture"));
            const auto participants = manager.chatParticipantContributions();
            QCOMPARE(participants.size(), 1);
            QCOMPARE(participants.first().id, QString("tests.native.assistant"));
            QCOMPARE(participants.first().name, QString("test-assistant"));
            QCOMPARE(participants.first().fullName, QString("Test Assistant"));
            QCOMPARE(participants.first().commands.size(), 1);
            const auto hello = std::find_if(contributions.begin(), contributions.end(),
                                            [](const auto& command) {
                                                return command.id == "tests.native.hello";
                                            });
            QCOMPARE(hello->title, QString("Say hello"));
            QCOMPARE(hello->category, QString("Native test"));
            QSignalSpy messages(&manager, &ExtensionManager::informationMessage);
            QVERIFY(manager.executeCommand("tests.native.hello"));
            QTRY_COMPARE(messages.size(), 1);
            QTRY_VERIFY(manager.extensions()[0].active);
            QTRY_VERIFY(manager.commands().contains("tests.native.temporary"));
            QTRY_VERIFY(manager.commands().contains("tests.native.unretained"));
            const auto visibleCommands = manager.commandContributions();
            QVERIFY(std::none_of(visibleCommands.cbegin(), visibleCommands.cend(),
                                 [](const auto& command) {
                                     return command.id == "tests.native.temporary";
                                 }));
            QVERIFY(manager.executeCommand("tests.native.temporary"));
            QTRY_COMPARE(messages.size(), 2);
            QVERIFY(manager.executeCommand("tests.native.unretained"));
            QTRY_COMPARE(messages.size(), 3);
            QCOMPARE(messages.at(2).at(0).toString(),
                     QStringLiteral("Unretained command invoked"));
            QVERIFY(manager.executeCommand("tests.native.hostPid"));
            QTRY_COMPARE(messages.size(), 4);
            QVERIFY(messages.at(3).at(0).toString() !=
                    QString::number(QCoreApplication::applicationPid()));
            QVERIFY(manager.executeCommand("tests.native.workspace"));
            QTRY_COMPARE(messages.size(), 5);
            QCOMPARE(messages.at(4).at(0).toString(), directory.path());
            QSignalSpy locationResults(&manager, &ExtensionManager::commandResult);
            const auto locationId = manager.requestCommand(
                "tests.native.extensionLocation", QJsonArray{QStringLiteral("assets/icon.svg")});
            QVERIFY(locationId != 0);
            const auto locationResult = [&locationResults, locationId] {
                const auto it = std::find_if(locationResults.cbegin(), locationResults.cend(),
                    [locationId](const auto& item) {
                        return item.at(0).toULongLong() == locationId;
                    });
                return it == locationResults.cend() ? QList<QVariant>{} : *it;
            };
            QTRY_VERIFY(!locationResult().isEmpty());
            QVERIFY(locationResult().at(2).toString().isEmpty());
            const auto location = locationResult().at(1).value<QJsonValue>().toObject();
            QCOMPARE(location.value("id").toString(), QStringLiteral("tests.native"));
            QCOMPARE(QDir::cleanPath(location.value("path").toString()),
                     QDir::cleanPath(directory.path()));
            QCOMPARE(QUrl(location.value("uri").toString()).toLocalFile(),
                     location.value("path").toString());
            QCOMPARE(QDir::cleanPath(location.value("resource").toString()),
                     QDir::cleanPath(directory.filePath("assets/icon.svg")));
            manager.setWorkspace(directory.filePath("other"));
            QVERIFY(manager.executeCommand("tests.native.workspace"));
            QTRY_COMPARE(messages.size(), 6);
            QCOMPARE(messages.at(5).at(0).toString(), directory.filePath("other"));
            QVERIFY(manager.executeCommand("tests.native.disposeTemporary"));
            QTRY_VERIFY(!manager.commands().contains("tests.native.temporary"));
            QVERIFY(!manager.executeCommand("tests.native.temporary"));
            QSignalSpy commandResults(&manager, &ExtensionManager::commandResult);
            const auto successId = manager.requestCommand("tests.native.hello");
            const auto failureId = manager.requestCommand("tests.native.throw");
            QVERIFY(successId != 0);
            QVERIFY(failureId > successId);
            const auto resultFor = [&commandResults](quint64 id) {
                const auto it = std::find_if(commandResults.cbegin(), commandResults.cend(),
                    [id](const auto& item) { return item.at(0).toULongLong() == id; });
                return it == commandResults.cend() ? QList<QVariant>{} : *it;
            };
            QTRY_VERIFY(!resultFor(successId).isEmpty() && !resultFor(failureId).isEmpty());
            QVERIFY(resultFor(successId).at(2).toString().isEmpty());
            QCOMPARE(resultFor(failureId).at(2).toString(),
                     QStringLiteral("fixture failure"));
            const auto echoId = manager.requestCommand(
                "tests.native.echo", QJsonArray{QStringLiteral("alpha"), 23});
            QVERIFY(echoId > failureId);
            QTRY_VERIFY(!resultFor(echoId).isEmpty());
            QCOMPARE(resultFor(echoId).at(1).value<QJsonValue>(), QJsonValue("alpha"));
            const auto futureId = manager.requestCommand("tests.native.future");
            QVERIFY(futureId > echoId);
            QTRY_VERIFY(!resultFor(futureId).isEmpty());
            QCOMPARE(resultFor(futureId).at(1).value<QJsonValue>(),
                     QJsonValue("future result"));
            const auto duplicateId = manager.requestCommand("tests.native.duplicateCheck");
            QVERIFY(duplicateId > futureId);
            QTRY_VERIFY(!resultFor(duplicateId).isEmpty());
            QCOMPARE(resultFor(duplicateId).at(1).value<QJsonValue>(),
                     QJsonValue("duplicate rejected"));
            QVERIFY(manager.setEnabled("tests.native", false));
            QVERIFY(manager.commands().isEmpty());
            QVERIFY(manager.chatParticipantContributions().isEmpty());
            QVERIFY(manager.debuggerContributions().isEmpty());
            QVERIFY(!manager.executeCommand("tests.native.hello"));
            QVERIFY(manager.setEnabled("tests.native", true));
            QVERIFY(manager.executeCommand("tests.native.hello"));
            QTRY_VERIFY(manager.commands().contains("tests.native.temporary"));
            QSignalSpy interrupted(&manager, &ExtensionManager::commandResult);
            const auto interruptedId = manager.requestCommand("tests.native.exitHost");
            QVERIFY(interruptedId != 0);
            QTRY_VERIFY(!manager.extensions()[0].active);
            QTRY_VERIFY([&] {
                return std::any_of(interrupted.cbegin(), interrupted.cend(),
                                   [interruptedId](const auto& result) {
                    return result.at(0).toULongLong() == interruptedId;
                });
            }());
            const auto interruptedResult = std::find_if(
                interrupted.cbegin(), interrupted.cend(), [interruptedId](const auto& result) {
                    return result.at(0).toULongLong() == interruptedId;
                });
            QVERIFY(interruptedResult != interrupted.cend());
            QVERIFY(!interruptedResult->at(2).toString().isEmpty());
            QVERIFY(manager.extensions()[0].error.contains("exited"));
            QVERIFY(manager.extensions()[0].enabled);
            QVERIFY(manager.commands().isEmpty());
            QVERIFY(manager.commandContributions().isEmpty());
            QVERIFY(!manager.executeCommand("tests.native.hello"));
        }
        ExtensionManager restarted(directory.path());
        restarted.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        restarted.discover();
        QVERIFY(restarted.extensions()[0].enabled);
        QVERIFY(restarted.executeCommand("tests.native.hello"));
        QTRY_VERIFY(restarted.extensions()[0].active);
    }
    void disablingBusyHostDoesNotBlockEditor() {
        QTemporaryDir directory;
        const QString target = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, target));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy messages(&manager, &ExtensionManager::informationMessage);
        QVERIFY(manager.executeCommand("tests.native.hello"));
        QTRY_VERIFY(manager.extensions()[0].active);
        QTRY_COMPARE(messages.size(), 1);
        messages.clear();
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        const auto marker = directory.filePath("host-blocked.marker");
        const auto blockedId = manager.requestCommand("tests.native.blockHost", {marker});
        QVERIFY(blockedId != 0);
        QTRY_VERIFY(QFileInfo::exists(marker));
        QVERIFY(results.isEmpty());
        const auto queuedId = manager.requestCommand("tests.native.hello");
        QVERIFY(queuedId > blockedId);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY2(timer.elapsed() < 250, "Disabling an extension blocked the editor thread.");
        QCOMPARE(results.size(), 2);
        QHash<quint64, QString> errors;
        for (const auto& result : results)
            errors.insert(result.at(0).toULongLong(), result.at(2).toString());
        QCOMPARE(errors.size(), 2);
        QCOMPARE(errors.value(blockedId), QStringLiteral("Extension disabled."));
        QCOMPARE(errors.value(queuedId), QStringLiteral("Extension disabled."));
        QVERIFY(!manager.extensions()[0].active);
        QVERIFY(!manager.executeCommand("tests.native.hello"));
    }
    void formatterSelectionIsExplicitWhenAmbiguous() {
        QTemporaryDir directory;
        const QString first = directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        const QString second =
            directory.filePath(QFileInfo(LITECODE_OTHER_FORMATTER_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, first));
        QVERIFY(QFile::copy(LITECODE_OTHER_FORMATTER_TEST_PLUGIN, second));
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QCOMPARE(manager.extensions().size(), 2);
        QVERIFY(manager.setEnabled("tests.native", true));
        QVERIFY(manager.setEnabled("tests.otherFormatter", true));
        manager.activateForLanguage("python");
        QTRY_VERIFY([&manager] {
            for (const auto& extension : manager.extensions())
                if (extension.id == "tests.otherFormatter") return extension.active;
            return false;
        }());
        QCOMPARE(manager.formatterContributions("python").size(), 2);
        QCOMPARE(manager.requestDocumentFormat("python", "hello"), quint64(0));
        QCOMPARE(manager.debuggerContributions().size(), 2);
        QCOMPARE(manager.requestDebugAdapter("fixture", {}), quint64(0));
        QSignalSpy results(&manager, &ExtensionManager::documentFormatResult);
        const auto id = manager.requestDocumentFormat("python", "hello",
                                                      "tests.otherFormatter");
        QVERIFY(id != 0);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results.at(0).at(1).toString(), QString("HELLO"));
        QCOMPARE(results.at(0).at(2).toString(), QString());
        QCOMPARE(manager.requestDocumentFormat("python", "hello", "missing"), quint64(0));
    }
};
QTEST_GUILESS_MAIN(ExtensionTests)
#include "tst_extensions.moc"
