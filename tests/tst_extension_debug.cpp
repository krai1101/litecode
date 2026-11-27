#include "extensions/ExtensionManager.h"
#include "extensions/DebugDescriptorWire.h"
#include "extensions/HostDebug.h"
#include "extensions/HostDebugConfigurations.h"
#include "extensions/HostDebugSessions.h"
#include "extensions/HostDebugTrackers.h"
#include "extensions/HostDebugStarts.h"
#include "extensions/DebugAdapterContribution.h"
#include "ui/RunDebugPanel.h"
#include "ui/debug/DebugConfigurationPicker.h"
#include "ui/debug/DebugTerminalController.h"
#include "ui/TerminalPanel.h"
#include "ui/TerminalView.h"
#include "ui/QuickOpenDialog.h"
#include <QLineEdit>
#include "SocketAdapterFixture.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
using namespace litecode;
class TestDebugFactory final : public extensions::DebugAdapterDescriptorFactory {
  public:
    std::function<extensions::ProviderResult<extensions::DebugAdapterDescriptor>()> callback;
    extensions::ProviderResult<extensions::DebugAdapterDescriptor> createDebugAdapterDescriptor(
        std::shared_ptr<extensions::DebugSession>, std::optional<extensions::DebugAdapterExecutable>) override { return callback(); }
};
class TestDebugTracker final : public extensions::DebugAdapterTracker {
  public:
    QStringList events;
    bool throws{};
    void onWillStartSession() override { events.append("start"); }
    void onWillReceiveMessage(const QJsonObject&) override { events.append("receive"); }
    void onDidSendMessage(const QJsonObject&) override { events.append("send"); if (throws) throw std::runtime_error("observer failure"); }
    void onWillStopSession() override { events.append("stop"); }
    void onError(const std::exception&) override { events.append("error"); }
    void onExit(std::optional<int>, std::optional<QString>) override { events.append("exit"); }
};
class TestDebugTrackerFactory final : public extensions::DebugAdapterTrackerFactory {
  public:
    std::function<extensions::ProviderResult<std::shared_ptr<extensions::DebugAdapterTracker>>()> callback;
    extensions::ProviderResult<std::shared_ptr<extensions::DebugAdapterTracker>> createDebugAdapterTracker(
        std::shared_ptr<extensions::DebugSession>) override { return callback(); }
};
class TestInlineAdapter final : public extensions::DebugAdapter {
  public:
    extensions::EventEmitter<QJsonObject> messages;
    int disposed{};
    extensions::Event<QJsonObject> onDidSendMessage() const override { return messages.event(); }
    void handleMessage(const QJsonObject&) override { throw std::runtime_error("inline handler failed"); }
    void dispose() override { ++disposed; messages.dispose(); }
};
class AsyncConfigurationProvider final : public extensions::DebugConfigurationProvider {
  public:
    std::shared_ptr<QPromise<extensions::DebugConfiguration>> promise = std::make_shared<QPromise<extensions::DebugConfiguration>>();
    extensions::CancellationToken token;
    AsyncConfigurationProvider() { promise->start(); }
    extensions::ProviderResult<extensions::DebugConfiguration> resolveDebugConfiguration(
        const std::optional<extensions::WorkspaceFolder>&, extensions::DebugConfiguration, extensions::CancellationToken cancellation) override {
        token = cancellation; return promise->future();
    }
};
class ExtensionDebugTests final : public QObject {
    Q_OBJECT
  private slots:
    void staleStartCancellationCannotStopSubsequentUiAttempt() {
        ui::RunDebugPanel panel;
        extensions::CancellationTokenSource cancellation;
        QSignalSpy requests(&panel, &ui::RunDebugPanel::debugConfigurationRequested);
        QSignalSpy canceled(&panel, &ui::RunDebugPanel::debugConfigurationCanceled);
        // Keep the provider pending so cancellation delivery can be ordered.
        connect(&panel, &ui::RunDebugPanel::debugConfigurationRequested, this,
            [](quint64, const QString&, const QString&, const QJsonObject&) {});
        const QJsonObject config{{"name", "Launch"}, {"type", "fixture"}, {"request", "launch"}};
        auto result = panel.startFromExtension(QJsonValue::Undefined, config, {}, cancellation.token());
        QCOMPARE(requests.size(), 1);
        cancellation.cancel();
        panel.completeDebugConfigurationRequest(requests.first()[0].toULongLong(), QJsonValue::Undefined, {});
        QVERIFY(result.isFinished()); QCOMPARE(result.result(), false);
        panel.startSelectedConfiguration(config);
        QCOMPARE(requests.size(), 2);
        QCoreApplication::processEvents();
        QVERIFY(canceled.isEmpty());
        panel.stop(); QCOMPARE(canceled.size(), 1);
    }
    void extensionStartHostShutdownSettlesPendingFuture() {
        QVector<QJsonObject> messages;
        extensions::HostDebug debug([&](const QJsonObject& value) { messages.append(value); });
        extensions::DebugSessionOptions options; options.noDebug = false; options.suppressSaveBeforeStart = true;
        auto result = debug.startDebugging({}, QJsonObject{{"name", "Native"}, {"type", "fixture"}}, options);
        QCOMPARE(messages.size(), 1); QVERIFY(!result.isFinished());
        QCOMPARE(messages.first().value("options").toObject().value("noDebug"), QJsonValue(false));
        QVERIFY(messages.first().value("folder").isUndefined());
        debug.shutdown(); QVERIFY(result.isFinished());
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, result.result());
        QVERIFY(debug.starts().accept({{"command", "debugStartResult"}, {"requestId", "1"}, {"error", ""}, {"result", true}}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, result.result());
    }
    void extensionStartOwnerLossCancelsWorkbenchAndDropsLateReply() {
        extensions::WorkbenchDebugStarts starts;
        QPromise<bool> promise; promise.start();
        extensions::CancellationToken cancellation;
        bool folderWasUndefined{};
        starts.setHandler([&](const QJsonValue& folder, const QJsonValue&, const QJsonObject&, extensions::CancellationToken token) {
            folderWasUndefined = folder.isUndefined(); cancellation = token; return promise.future();
        });
        QVector<QJsonObject> replies;
        const QJsonObject request{{"requestId", "1"}, {"configuration", QJsonObject{{"name", "Launch"}}}, {"options", QJsonObject{}}};
        QVERIFY(starts.request("owner", request, [&](const QJsonObject& reply) { replies.append(reply); }));
        QVERIFY(folderWasUndefined);
        QVERIFY(!cancellation.isCancellationRequested());
        QVERIFY(!starts.request("owner", request, [](const QJsonObject&) {}));
        starts.removeOwner("owner"); QVERIFY(cancellation.isCancellationRequested());
        promise.addResult(true); promise.finish(); QCoreApplication::processEvents();
        QVERIFY(replies.isEmpty());
    }
    void trackerHooksSurviveFactoryDisposalAndIsolateObserverExceptions() {
        using namespace extensions;
        QVector<QJsonObject> messages; HostDebug service([&](const QJsonObject& value) { messages.append(value); });
        const auto first = std::make_shared<TestDebugTracker>(); first->throws = true;
        const auto second = std::make_shared<TestDebugTracker>();
        auto typed = std::make_shared<TestDebugTrackerFactory>(); typed->callback = [first] { return std::shared_ptr<DebugAdapterTracker>(first); };
        auto wildcard = std::make_shared<TestDebugTrackerFactory>(); wildcard->callback = [second] { return std::shared_ptr<DebugAdapterTracker>(second); };
        auto typedRegistration = service.registerDebugAdapterTrackerFactory("fixture", typed);
        auto wildcardRegistration = service.registerDebugAdapterTrackerFactory("*", wildcard);
        const auto session = service.sessions().session({{"id", "tracker"}, {"type", "fixture"}, {"name", "Launch"},
            {"configuration", QJsonObject{{"type", "fixture"}}}, {"workspaceFolder", QJsonValue::Null}});
        service.trackers().prepare(1, session); QCOMPARE(messages.last().value("event").toString(), QString("debugTrackersReady"));
        typedRegistration.dispose(); wildcardRegistration.dispose();
        for (const auto& kind : {QString("start"), QString("receive"), QString("send"), QString("error"), QString("stop"), QString("exit")})
            QVERIFY(service.trackers().event(1, {{"kind", kind}, {"message", QJsonObject{{"type", "request"}}}, {"error", "adapter error"}, {"code", 0}}));
        const QStringList expected{"start", "receive", "send", "error", "stop", "exit"};
        QCOMPARE(first->events, expected); QCOMPARE(second->events, expected);
        QVERIFY(service.trackers().event(1, {{"kind", "exit"}})); QCOMPARE(first->events, expected);
        bool diagnostic{};
        for (const auto& message : messages) diagnostic |= message.value("event") == QJsonValue("debugTrackerDiagnostic");
        QVERIFY(diagnostic);
    }
    void trackerCollectionTimeoutDropsLateResultsAndRetainsFactoryLifetime() {
        using namespace extensions;
        QVector<QJsonObject> messages; HostDebug service([&](const QJsonObject& value) { messages.append(value); });
        auto promise = std::make_shared<QPromise<std::shared_ptr<DebugAdapterTracker>>>(); promise->start();
        auto factory = std::make_shared<TestDebugTrackerFactory>(); factory->callback = [promise] { return promise->future(); };
        const std::weak_ptr<TestDebugTrackerFactory> lifetime(factory);
        auto registration = service.registerDebugAdapterTrackerFactory("fixture", factory);
        const auto session = service.sessions().session({{"id", "slow"}, {"type", "fixture"}, {"name", "Launch"},
            {"configuration", QJsonObject{{"type", "fixture"}}}, {"workspaceFolder", QJsonValue::Null}});
        service.trackers().prepare(1, session); registration.dispose(); factory.reset(); QVERIFY(!lifetime.expired());
        QTRY_COMPARE_WITH_TIMEOUT(messages.last().value("event").toString(), QString("debugTrackersReady"), 3000);
        const auto tracker = std::make_shared<TestDebugTracker>();
        promise->addResult(tracker); promise->finish(); QTRY_VERIFY(lifetime.expired());
        QVERIFY(service.trackers().event(1, {{"kind", "start"}})); QVERIFY(tracker->events.isEmpty());
        service.trackers().release(1); service.shutdown();
    }
    void sessionFacadesPreserveIdentityResultsAndTermination() {
        using namespace extensions;
        QVector<QJsonObject> messages;
        HostDebug service([&](const QJsonObject& message) { messages.append(message); });
        const QJsonObject context{{"id", "session-1"}, {"type", "fixture"}, {"name", "Launch"},
            {"workspaceFolder", QJsonValue::Null}, {"configuration", QJsonObject{{"type", "fixture"}}}};
        const auto facade = service.sessions().session(context);
        int starts{}, ends{}, activeChanges{};
        auto started = service.onDidStartDebugSession()([&](const auto& session) { ++starts; QCOMPARE(session, facade); });
        auto ended = service.onDidTerminateDebugSession()([&](const auto& session) { ++ends; QCOMPARE(session, facade); });
        auto changed = service.onDidChangeActiveDebugSession()([&](const auto&) { ++activeChanges; });
        QVERIFY(service.sessions().accept({{"command", "debugSessionEvent"}, {"kind", "started"}, {"session", context}}));
        QVERIFY(service.sessions().accept({{"command", "debugSessionEvent"}, {"kind", "started"}, {"session", context}}));
        QCOMPARE(starts, 1);
        QVERIFY(service.sessions().accept({{"command", "debugSessionEvent"}, {"kind", "active"}, {"session", context}}));
        QCOMPARE(service.activeDebugSession(), facade); QCOMPARE(activeChanges, 1);
        auto result = facade->customRequest("echo", QJsonArray{1, "two"});
        QCOMPARE(messages.last().value("arguments").toArray(), (QJsonArray{1, "two"}));
        const auto id = messages.last().value("requestId");
        QVERIFY(service.sessions().accept({{"command", "debugSessionResult"}, {"requestId", id}, {"error", ""}, {"result", 42}}));
        QVERIFY(result.isFinished()); QCOMPARE(result.result(), QJsonValue(42));
        auto waiting = facade->customRequest("pending"); QVERIFY(!waiting.isFinished());
        QVERIFY(!messages.last().contains("arguments"));
        QVERIFY(service.sessions().accept({{"command", "debugSessionEvent"}, {"kind", "terminated"}, {"session", context}}));
        QVERIFY(waiting.isFinished()); QVERIFY_THROWS_EXCEPTION(std::exception, waiting.result());
        QCOMPARE(ends, 1); QCOMPARE(activeChanges, 2); QVERIFY(!service.activeDebugSession());
        QVERIFY(service.sessions().accept({{"command", "debugSessionEvent"}, {"kind", "terminated"}, {"session", context}}));
        QCOMPARE(ends, 1);
        auto stale = facade->customRequest("echo"); QVERIFY(stale.isFinished()); QVERIFY_THROWS_EXCEPTION(std::exception, stale.result());
        started.dispose(); ended.dispose(); changed.dispose();
    }
    void lateHostSnapshotDoesNotReplayStartAndShutdownSettlesRequests() {
        using namespace extensions;
        QVector<QJsonObject> messages; HostDebug service([&](const QJsonObject& value) { messages.append(value); });
        int starts{}; auto started = service.onDidStartDebugSession()([&](const auto&) { ++starts; });
        const QJsonObject context{{"id", "running"}, {"type", "fixture"}, {"name", "Launch"},
            {"workspaceFolder", QJsonValue::Null}, {"configuration", QJsonObject{{"type", "fixture"}}}};
        QVERIFY(service.sessions().accept({{"command", "debugSessionEvent"}, {"kind", "snapshot"}, {"session", context}}));
        QVERIFY(service.sessions().accept({{"command", "debugSessionEvent"}, {"kind", "active"}, {"session", context}}));
        const auto facade = service.activeDebugSession(); QVERIFY(facade); QCOMPARE(starts, 0);
        auto waiting = facade->customRequest("waiting"); service.shutdown();
        QVERIFY(waiting.isFinished()); QVERIFY_THROWS_EXCEPTION(std::exception, waiting.result());
        QVERIFY(!service.activeDebugSession());
        auto stale = facade->customRequest("late"); QVERIFY(stale.isFinished()); QVERIFY_THROWS_EXCEPTION(std::exception, stale.result());
        started.dispose();
    }
    void configurationProviderDisposalCancelsAndKeepsAsyncLifetime() {
        using namespace extensions;
        QVector<QJsonObject> messages; HostDebug service([&](const QJsonObject& value) { messages.append(value); });
        auto provider = std::make_shared<AsyncConfigurationProvider>();
        const auto promise = provider->promise; const std::weak_ptr<AsyncConfigurationProvider> lifetime(provider);
        auto registration = service.registerDebugConfigurationProvider("fixture", provider, DebugConfigurationProviderTriggerKind::Initial);
        const auto handle = messages.last().value("handle").toString().toULongLong(); QVERIFY(handle);
        service.configurations().invoke(1, handle, "resolveDebugConfiguration", {}, QJsonObject{{"type", "fixture"}});
        const auto token = provider->token; QVERIFY(!token.isCancellationRequested());
        registration.dispose(); registration.dispose(); provider.reset();
        QVERIFY(token.isCancellationRequested()); QVERIFY(!lifetime.expired());
        QCOMPARE(messages.size(), 3); QCOMPARE(messages[1].value("resultKind").toString(), QString("undefined"));
        promise->addResult(QJsonObject{{"type", "fixture"}}); promise->finish();
        QTRY_VERIFY(lifetime.expired()); QCOMPARE(messages.size(), 3);
    }
    void separateHostsProvideChainAndPreserveNullVersusUndefined() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        for (const QString path : {QString(LITECODE_TEST_PLUGIN), QString(LITECODE_DEFAULT_DEBUG_PLUGIN)})
            QVERIFY(QFile::copy(path, directory.filePath(QFileInfo(path).fileName())));
        extensions::ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(directory.path()); manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true)); QVERIFY(manager.setEnabled("tests.defaultDebug", true));
        QSignalSpy results(&manager, &extensions::ExtensionManager::debugConfigurationResult);
        const auto provided = manager.requestDebugConfiguration("fixture", "provideDebugConfigurations", QJsonValue(QJsonValue::Undefined));
        QVERIFY(provided); QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 15000);
        QVERIFY2(results[0][2].toString().isEmpty(), qPrintable(results[0][2].toString()));
        const auto configurations = results[0][1].value<QJsonValue>().toArray(); QCOMPARE(configurations.size(), 1);
        auto configuration = configurations[0].toObject(); QCOMPARE(configuration.value("folderSeen").toString(), QUrl::fromLocalFile(directory.path()).toString());
        QVERIFY(manager.requestDebugConfiguration("fixture", "resolveDebugConfiguration", configuration));
        QTRY_COMPARE(results.size(), 2); QVERIFY(results[1][2].toString().isEmpty());
        configuration = results[1][1].value<QJsonValue>().toObject();
        QCOMPARE(configuration.value("stages").toArray(), (QJsonArray{"before:default", "before:native"}));
        debug::VariableContext variables; variables.workspaceFolder = directory.path(); QString error;
        const auto substituted = debug::DebugConfigurations::resolve(configuration, variables, &error); QVERIFY(error.isEmpty());
        QVERIFY(manager.requestDebugConfiguration("fixture", "resolveDebugConfigurationWithSubstitutedVariables", substituted));
        QTRY_COMPARE(results.size(), 3); QVERIFY(results[2][2].toString().isEmpty());
        QCOMPARE(results[2][1].value<QJsonValue>().toObject().value("stages").toArray(),
            (QJsonArray{"before:default", "before:native", "after:default", "after:native"}));
        for (const auto& kind : {QString("null"), QString("undefined")}) {
            configuration.insert("cancelAs", kind);
            const auto id = manager.requestDebugConfiguration("fixture", "resolveDebugConfiguration", configuration); QVERIFY(id);
            const auto expected = results.size() + 1; QTRY_COMPARE(results.size(), expected);
            QVERIFY(results.last()[2].toString().isEmpty());
            const auto result = results.last()[1].value<QJsonValue>();
            if (kind == "null") QVERIFY(result.isNull()); else QVERIFY(result.isUndefined());
        }
        const auto dynamic = manager.requestDebugConfiguration("fixture", "provideDebugConfigurations", {}, extensions::DebugConfigurationProviderTriggerKind::Dynamic);
        QVERIFY(dynamic); QTRY_COMPARE(results.size(), 6);
        const auto dynamicConfigurations = results.last()[1].value<QJsonValue>().toArray(); QCOMPARE(dynamicConfigurations.size(), 1);
        QCOMPARE(dynamicConfigurations[0].toObject().value("name").toString(), QString("Dynamic native launch"));
        QVERIFY(manager.setEnabled("tests.native", false)); QVERIFY(manager.setEnabled("tests.defaultDebug", false));
    }
    void sharedConfigurationPickerCreatesLaunchAndSelectsDynamicWithoutOverwriting() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        extensions::ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(directory.path()); manager.discover(); QVERIFY(manager.setEnabled("tests.native", true));
        QWidget window; window.resize(1000, 700); window.show();
        ui::DebugConfigurationPicker controller(&manager, [&] { return directory.path(); }, &window);
        QSignalSpy opened(&controller, &ui::DebugConfigurationPicker::openConfiguration);
        controller.createLaunch();
        auto* picker = window.findChild<ui::QuickOpenDialog*>(); QVERIFY(picker);
        QTest::keyClick(picker->findChild<QLineEdit*>(), Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 15000);
        QFile launch(directory.filePath(".vscode/launch.json")); QVERIFY(launch.open(QIODevice::ReadOnly));
        const auto generated = launch.readAll(); launch.close();
        const auto configurations = QJsonDocument::fromJson(generated).object().value("configurations").toArray(); QCOMPARE(configurations.size(), 1);
        QCOMPARE(configurations[0].toObject().value("name").toString(), QString("Generated native launch"));
        QVERIFY(launch.open(QIODevice::WriteOnly)); const auto preserved = QByteArray("// user's existing configuration\n") + generated;
        QCOMPARE(launch.write(preserved), preserved.size()); launch.close();
        controller.createLaunch(); QCOMPARE(opened.size(), 2);
        QVERIFY(launch.open(QIODevice::ReadOnly)); QCOMPARE(launch.readAll(), preserved); launch.close();
        QSignalSpy selected(&controller, &ui::DebugConfigurationPicker::configurationSelected);
        controller.selectAndStart();
        QTRY_VERIFY(window.findChild<ui::QuickOpenDialog*>());
        picker = window.findChild<ui::QuickOpenDialog*>();
        QTest::keyClick(picker->findChild<QLineEdit*>(), Qt::Key_Return);
        QTRY_VERIFY(window.findChild<ui::QuickOpenDialog*>());
        QTRY_VERIFY(window.findChild<ui::QuickOpenDialog*>()
            && window.findChild<ui::QuickOpenDialog*>()->windowTitle() == QString("Select a debug configuration"));
        picker = window.findChild<ui::QuickOpenDialog*>(); QTest::keyClick(picker->findChild<QLineEdit*>(), Qt::Key_Return);
        QTRY_COMPARE(selected.size(), 1);
        QCOMPARE(selected[0][0].value<QJsonObject>().value("name").toString(), QString("Dynamic native launch"));
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void canceledAsyncInlineResultIsDisposedWithoutSecondCompletion() {
        using namespace extensions;
        QVector<QJsonObject> messages;
        HostDebug service([&](const QJsonObject& message) { messages.append(message); });
        auto promise = std::make_shared<QPromise<DebugAdapterDescriptor>>(); promise->start();
        auto factory = std::make_shared<TestDebugFactory>(); factory->callback = [promise] { return promise->future(); };
        auto registration = service.registerDebugAdapterDescriptorFactory("fixture", factory);
        const QJsonObject context{{"id", "session"}, {"type", "fixture"}, {"name", "Session"},
            {"configuration", QJsonObject{{"type", "fixture"}}}, {"workspaceFolder", QJsonValue::Null}};
        service.resolve(1, context, {}); service.release(1); service.release(1);
        QCOMPARE(messages.size(), 2); QVERIFY(messages.last().value("error").toString().contains("canceled"));
        auto adapter = std::make_shared<TestInlineAdapter>();
        promise->addResult(DebugAdapterInlineImplementation{adapter}); promise->finish();
        QTRY_COMPARE(adapter->disposed, 1); QCOMPARE(messages.size(), 2);
        registration.dispose(); service.shutdown(); QCOMPARE(adapter->disposed, 1);
    }
    void inlineHandlerFailureAndHostShutdownDisposeExactlyOnce() {
        using namespace extensions;
        QVector<QJsonObject> messages;
        HostDebug service([&](const QJsonObject& message) { messages.append(message); });
        auto adapter = std::make_shared<TestInlineAdapter>();
        auto factory = std::make_shared<TestDebugFactory>();
        factory->callback = [adapter] { return DebugAdapterDescriptor(DebugAdapterInlineImplementation{adapter}); };
        auto registration = service.registerDebugAdapterDescriptorFactory("fixture", factory);
        const QJsonObject context{{"id", "session"}, {"type", "fixture"}, {"name", "Session"},
            {"configuration", QJsonObject{{"type", "fixture"}}}, {"workspaceFolder", QJsonValue::Null}};
        service.resolve(1, context, {});
        const auto descriptor = messages.last().value("descriptor").toObject();
        QCOMPARE(descriptor.value("type").toString(), QString("implementation"));
        QLocalSocket peer; peer.connectToServer(descriptor.value("endpoint").toString());
        QTRY_COMPARE(peer.state(), QLocalSocket::ConnectedState);
        peer.write(debug::DapDecoder::encode({{"type", "request"}, {"seq", 1}, {"command", "initialize"}}));
        QTRY_COMPARE(adapter->disposed, 1);
        QTRY_COMPARE(peer.state(), QLocalSocket::UnconnectedState);
        service.release(1); service.shutdown(); QCOMPARE(adapter->disposed, 1);
        auto abandoned = std::make_shared<TestInlineAdapter>();
        HostDebug second([](const QJsonObject&) {});
        auto secondFactory = std::make_shared<TestDebugFactory>();
        secondFactory->callback = [abandoned] { return DebugAdapterDescriptor(DebugAdapterInlineImplementation{abandoned}); };
        auto secondRegistration = second.registerDebugAdapterDescriptorFactory("fixture", secondFactory);
        second.resolve(2, context, {}); second.shutdown(); second.shutdown(); QCOMPARE(abandoned->disposed, 1);
    }
    void separateHostInlineDescriptorCanBeReleasedBeforeConnection() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        extensions::ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover(); QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &extensions::ExtensionManager::debugAdapterResult);
        const auto id = manager.requestDebugAdapter("fixture", {{"inlineFixture", true}}); QVERIFY(id);
        QTRY_COMPARE(results.size(), 1); QVERIFY(results[0][2].toString().isEmpty());
        const auto endpoint = results[0][1].value<QJsonObject>().value("endpoint").toString(); QVERIFY(!endpoint.isEmpty());
        QVERIFY(manager.releaseDebugAdapter(id)); QVERIFY(!manager.releaseDebugAdapter(id));
        QSignalSpy commands(&manager, &extensions::ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.debugFactoryContext")); QTRY_COMPARE(commands.size(), 1);
        QCOMPARE(commands[0][1].value<QJsonValue>().toObject().value("inlineDisposed").toInt(), 1);
        QLocalSocket latePeer; latePeer.connectToServer(endpoint);
        QTRY_VERIFY(latePeer.state() != QLocalSocket::ConnectingState);
        QCOMPARE(latePeer.state(), QLocalSocket::UnconnectedState);
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void manifestExecutableSelectionMatchesAllThreePlatforms() {
        using namespace extensions;
        QTemporaryDir root; QVERIFY(root.isValid());
        const QJsonObject contribution{{"program", "base-adapter"}, {"args", QJsonArray{"base-arg"}},
            {"runtime", "runtime-name"}, {"runtimeArgs", QJsonArray{"base-runtime-arg"}},
            {"windows", QJsonObject{{"program", "windows-adapter"}, {"args", QJsonArray{}}}},
            {"win", QJsonObject{{"runtime", "./win-runtime"}}},
            {"winx86", QJsonObject{{"program", "x86-adapter"}}},
            {"osx", QJsonObject{{"program", "mac-adapter"}, {"runtime", ""}, {"runtimeArgs", QJsonArray{}}}},
            {"linux", QJsonObject{{"runtime", "./linux-runtime"}, {"args", QJsonArray{"linux-arg"}}}}};
        const auto windows = debugAdapterExecutableFromContribution(contribution, root.path(), DebugAdapterPlatform::Windows, false);
        QVERIFY(windows); QCOMPARE(windows->command, QString("runtime-name"));
        QCOMPARE(windows->args, QStringList({"base-runtime-arg", QDir(root.path()).absoluteFilePath("x86-adapter"), "base-arg"}));
        const auto wow64 = debugAdapterExecutableFromContribution(contribution, root.path(), DebugAdapterPlatform::Windows, true);
        QVERIFY(wow64); QCOMPARE(wow64->command, QDir(root.path()).absoluteFilePath("./win-runtime"));
        const auto mac = debugAdapterExecutableFromContribution(contribution, root.path(), DebugAdapterPlatform::MacOS);
        QVERIFY(mac); QCOMPARE(mac->command, QString("runtime-name"));
        QCOMPARE(mac->args, QStringList({QDir(root.path()).absoluteFilePath("mac-adapter"), "base-arg"}));
        const auto linux = debugAdapterExecutableFromContribution(contribution, root.path(), DebugAdapterPlatform::Linux);
        QVERIFY(linux); QCOMPARE(linux->command, QDir(root.path()).absoluteFilePath("./linux-runtime"));
        QCOMPARE(linux->args, QStringList({"base-runtime-arg", QDir(root.path()).absoluteFilePath("base-adapter"), "linux-arg"}));
        const auto plain = debugAdapterExecutableFromContribution({{"program", "adapter"}}, root.path(), DebugAdapterPlatform::Linux);
        QVERIFY(plain); QCOMPARE(plain->command, QDir(root.path()).absoluteFilePath("adapter")); QVERIFY(plain->args.isEmpty());
        QVERIFY(!debugAdapterExecutableFromContribution({}, root.path(), DebugAdapterPlatform::Linux));
        auto invalid = contribution; invalid.insert("linux", QJsonObject{{"args", QJsonArray{42}}});
        QVERIFY(!validateDebugAdapterContribution(invalid).isEmpty());
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, debugAdapterExecutableFromContribution(invalid, root.path(), DebugAdapterPlatform::Windows));
        const auto legacyWindows = debugAdapterExecutableFromContribution({{"program", "base"}, {"args", QJsonArray{"base"}},
            {"windows", QJsonObject{{"program", "override"}, {"args", QJsonArray{}}}}}, root.path(), DebugAdapterPlatform::Windows);
        QVERIFY(legacyWindows); QCOMPARE(legacyWindows->command, QDir(root.path()).absoluteFilePath("override")); QVERIFY(legacyWindows->args.isEmpty());
    }
    void manifestDefaultStartsActualSessionWithAndWithoutFactory_data() {
        QTest::addColumn<QString>("type");
        QTest::newRow("manifest-without-factory") << QString("packaged");
        QTest::newRow("factory-receives-manifest-executable") << QString("packagedFactory");
    }
    void manifestDefaultStartsActualSessionWithAndWithoutFactory() {
        QFETCH(QString, type);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        for (const QString source : {QString(LITECODE_DEFAULT_DEBUG_PLUGIN), QString(LITECODE_DAP_ADAPTER_FIXTURE)})
            QVERIFY(QFile::copy(source, directory.filePath(QFileInfo(source).fileName())));
        QVERIFY(QDir(directory.path()).mkdir(".vscode"));
        const QJsonObject config{{"name", "Packaged adapter"}, {"type", type}, {"request", "launch"}};
        QFile launch(directory.filePath(".vscode/launch.json")); QVERIFY(launch.open(QIODevice::WriteOnly));
        launch.write(QJsonDocument(QJsonObject{{"configurations", QJsonArray{config}}}).toJson()); launch.close();
        extensions::ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover(); QCOMPARE(manager.extensions().size(), 1);
        QVERIFY2(manager.extensions()[0].error.isEmpty(), qPrintable(manager.extensions()[0].error));
        QVERIFY(manager.setEnabled("tests.defaultDebug", true));
        ui::RunDebugPanel panel; panel.setWorkspace(directory.path());
        connect(&panel, &ui::RunDebugPanel::debugSessionEvent, &manager, &extensions::ExtensionManager::publishDebugSessionEvent);
        manager.setDebugSessionHandler([&panel](const QString& id, const QString& operation, const QString& command, const QJsonValue& args) {
            return panel.handleDebugSessionRequest(id, operation, command, args);
        });
        QHash<quint64, quint64> requests;
        connect(&panel, &ui::RunDebugPanel::debugAdapterRequested, &manager,
            [&](quint64 local, const QString& debugType, const QJsonObject& configuration, const QString& sessionId) {
                const auto id = manager.requestDebugAdapter(debugType, configuration, sessionId); QVERIFY(id); requests.insert(id, local);
            });
        connect(&manager, &extensions::ExtensionManager::debugAdapterResult, &panel,
            [&](quint64 id, const QJsonObject& descriptor, const QString& error) {
                QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(requests.contains(id));
                QCOMPARE(descriptor.value("command").toString(), directory.filePath(QFileInfo(LITECODE_DAP_ADAPTER_FIXTURE).fileName()));
                QCOMPARE(descriptor.value("args").toArray(), QJsonArray{"--session"});
                panel.completeDebugAdapterRequest(requests.take(id), descriptor, error);
            });
        panel.startDebugging(); auto* notice = panel.findChild<QLabel*>("runDebugNotice"); QVERIFY(notice);
        QTRY_COMPARE_WITH_TIMEOUT(notice->text(), QString("Debug state: 4"), 15000);
        panel.pauseDebugging(); QTRY_VERIFY(panel.isPaused());
        panel.evaluateConsole("answer"); QTRY_VERIFY(!panel.outputText().isEmpty());
        panel.stop(); QTRY_COMPARE(notice->text(), QString("Debug state: 0"));
        QVERIFY(manager.setEnabled("tests.defaultDebug", false));
    }
    void disposedAsyncFactorySettlesOnceAndRetainsNativeLifetime() {
        using namespace extensions;
        QVector<QJsonObject> messages;
        HostDebug service([&](const QJsonObject& message) { messages.append(message); });
        auto promise = std::make_shared<QPromise<DebugAdapterDescriptor>>(); promise->start();
        auto factory = std::make_shared<TestDebugFactory>();
        factory->callback = [promise] { return promise->future(); };
        const std::weak_ptr<TestDebugFactory> lifetime(factory);
        auto registration = service.registerDebugAdapterDescriptorFactory("fixture", factory);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, service.registerDebugAdapterDescriptorFactory("fixture", factory));
        const QJsonObject context{{"id", "session"}, {"type", "fixture"}, {"name", "Session"},
            {"configuration", QJsonObject{{"type", "fixture"}}}, {"workspaceFolder", QJsonValue::Null}};
        service.resolve(1, context, {}); registration.dispose(); factory.reset();
        QVERIFY(!lifetime.expired());
        QCOMPARE(messages.size(), 3); // registration, one failed request, deregistration
        QCOMPARE(messages[1].value("requestId").toString(), QString("1"));
        QVERIFY(messages[1].value("error").toString().contains("disposed"));
        promise->addResult(DebugAdapterServer{4711, {}}); promise->finish();
        QTRY_VERIFY(lifetime.expired()); QCOMPARE(messages.size(), 3);
    }
    void asyncFactoryExceptionsAndDefaultExecutableArePreserved() {
        using namespace extensions;
        QVector<QJsonObject> messages; HostDebug service([&](const QJsonObject& message) { messages.append(message); });
        const QJsonObject context{{"id", "session"}, {"type", "fixture"}, {"name", "Session"},
            {"configuration", QJsonObject{{"type", "fixture"}}}, {"workspaceFolder", QJsonValue::Null}};
        service.resolve(1, context, DebugAdapterExecutable{"default-adapter", {}, {}});
        QCOMPARE(messages.size(), 1); QCOMPARE(messages.last().value("descriptor").toObject().value("command").toString(), QString("default-adapter"));
        auto promise = std::make_shared<QPromise<DebugAdapterDescriptor>>(); promise->start();
        auto factory = std::make_shared<TestDebugFactory>(); factory->callback = [promise] { return promise->future(); };
        auto registration = service.registerDebugAdapterDescriptorFactory("fixture", factory);
        service.resolve(2, context, {});
        promise->setException(std::make_exception_ptr(std::runtime_error("asynchronous factory failure"))); promise->finish();
        QTRY_COMPARE(messages.size(), 3);
        QCOMPARE(messages.last().value("error").toString(), QString("asynchronous factory failure"));
        registration.dispose();
    }
    void descriptorWireRejectsMalformedValuesAndKeepsOptionsSeparate() {
        using namespace extensions;
        DebugAdapterExecutableOptions options;
        options.cwd = "/adapter-directory"; options.env = QMap<QString, QString>{{"ADAPTER_ONLY", "literal & |"}};
        const auto encoded = wire::encodeDebugAdapterDescriptor(DebugAdapterExecutable{"adapter", {"literal quotes \""}, options});
        const auto decoded = wire::decodeDebugAdapterDescriptor(encoded); QVERIFY(decoded);
        QCOMPARE(std::get<DebugAdapterExecutable>(*decoded).options->cwd, options.cwd);
        QCOMPARE(std::get<DebugAdapterExecutable>(*decoded).options->env, options.env);
        auto invalid = encoded; invalid.insert("args", QJsonArray{42}); QVERIFY(!wire::decodeDebugAdapterDescriptor(invalid));
        invalid = encoded; invalid.insert("options", QJsonObject{{"env", QJsonObject{{"KEY", QJsonValue::Null}}}});
        QVERIFY(!wire::decodeDebugAdapterDescriptor(invalid));
        invalid = encoded; invalid.insert("options", QJsonObject{{"env", QJsonObject{{"BAD=KEY", "value"}}}});
        QVERIFY(!wire::decodeDebugAdapterDescriptor(invalid));
        for (const double port : {0.0, -1.0, 65536.0, 1.5})
            QVERIFY(!wire::decodeDebugAdapterDescriptor({{"type", "server"}, {"port", port}}));
        QVERIFY(!wire::decodeDebugAdapterDescriptor({{"type", "pipeServer"}, {"path", ""}}));
        QVERIFY(!wire::decodeDebugAdapterDescriptor({{"type", "implementation"}}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, wire::encodeDebugAdapterDescriptor(DebugAdapterInlineImplementation{}));
    }
    void nativeFactoryReceivesSessionAndAsynchronousExecutableOptions() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        extensions::ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(directory.path()); manager.discover(); QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &extensions::ExtensionManager::debugAdapterResult);
        const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QJsonObject config{{"name", "Factory session"}, {"type", "fixture"}, {"request", "launch"},
            {"adapterForTest", LITECODE_DAP_ADAPTER_FIXTURE}, {"asyncDescriptor", true},
            {"adapterEnvironment", QJsonObject{{"ADAPTER_ONLY", "yes"}}}, {"adapterCwd", directory.path()},
            {"env", QJsonObject{{"TARGET_ONLY", "yes"}}}, {"cwd", "/target-directory"}};
        const auto id = manager.requestDebugAdapter("fixture", config, sessionId); QVERIFY(id);
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 15000);
        QCOMPARE(results[0][0].toULongLong(), id);
        QVERIFY2(results[0][2].toString().isEmpty(), qPrintable(results[0][2].toString()));
        const auto descriptor = extensions::wire::decodeDebugAdapterDescriptor(results[0][1].value<QJsonObject>());
        QVERIFY(descriptor); const auto executable = std::get<extensions::DebugAdapterExecutable>(*descriptor);
        QCOMPARE(executable.command, QString(LITECODE_DAP_ADAPTER_FIXTURE));
        QCOMPARE(*executable.options->cwd, directory.path());
        QCOMPARE(*executable.options->env, (QMap<QString, QString>{{"ADAPTER_ONLY", "yes"}}));
        QSignalSpy commands(&manager, &extensions::ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.debugFactoryContext")); QTRY_COMPARE(commands.size(), 1);
        QVERIFY(commands[0][2].toString().isEmpty());
        const auto context = commands[0][1].value<QJsonValue>().toObject();
        QCOMPARE(context.value("id").toString(), sessionId);
        QCOMPARE(context.value("name").toString(), QString("Factory session"));
        QCOMPARE(context.value("configuration").toObject(), config);
        QCOMPARE(context.value("workspaceFolder").toObject().value("uri").toString(), QUrl::fromLocalFile(directory.path()).toString());
        QVERIFY(!context.value("hasExecutable").toBool());
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void absentFailureAndDebugServerOverrideCrossHost() {
        QTemporaryDir directory;
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        extensions::ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover(); QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &extensions::ExtensionManager::debugAdapterResult);
        QVERIFY(manager.requestDebugAdapter("fixture", {{"absentDescriptor", true}}));
        QTRY_COMPARE(results.size(), 1); QVERIFY(results.last()[2].toString().contains("descriptor"));
        QVERIFY(manager.requestDebugAdapter("fixture", {{"failDescriptor", true}}));
        QTRY_COMPARE(results.size(), 2); QCOMPARE(results.last()[2].toString(), QString("fixture factory failure"));
        QVERIFY(manager.requestDebugAdapter("fixture", {{"failDescriptor", true}, {"debugServer", 4711}}));
        QTRY_COMPARE(results.size(), 3); QVERIFY(results.last()[2].toString().isEmpty());
        QCOMPARE(results.last()[1].value<QJsonObject>(), (QJsonObject{{"type", "server"}, {"port", 4711}}));
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void nativeDescriptorStartsActualWorkbenchSession_data() {
        QTest::addColumn<QString>("transport");
        QTest::newRow("executable") << QString("executable");
        QTest::newRow("executable-adapter-owner-disabled") << QString("executable-owner-disabled");
        QTest::newRow("tcp") << QString("tcp");
        QTest::newRow("named-pipe-or-unix-socket") << QString("pipe");
        QTest::newRow("native-inline-implementation") << QString("inline");
        QTest::newRow("inline-prelaunch-failure") << QString("inline-prelaunch-failure");
        QTest::newRow("inline-configuration-provider-chain") << QString("inline-config-chain");
        QTest::newRow("inline-tracker-factory-throws") << QString("inline-tracker-throw");
        QTest::newRow("inline-tracker-factory-absent") << QString("inline-tracker-absent");
        QTest::newRow("inline-tracker-factory-times-out") << QString("inline-tracker-slow");
        QTest::newRow("inline-tracker-adapter-error") << QString("inline-tracker-error");
        QTest::newRow("inline-tracker-other-host-disabled") << QString("inline-observer-disable");
        QTest::newRow("extension-start-object") << QString("inline-start-object");
        QTest::newRow("extension-start-named") << QString("inline-start-named");
        QTest::newRow("extension-start-no-folder") << QString("inline-start-no-folder");
        QTest::newRow("extension-start-other-folder") << QString("inline-start-other-folder");
        QTest::newRow("extension-start-no-debug") << QString("inline-start-no-debug");
        QTest::newRow("extension-start-provider-canceled") << QString("inline-start-canceled");
        QTest::newRow("extension-start-prelaunch-failed") << QString("inline-start-failed");
        QTest::newRow("extension-start-interactive-terminal") << QString("inline-start-terminal");
    }
    void nativeDescriptorStartsActualWorkbenchSession() {
        QFETCH(QString, transport);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        if (transport == "inline-config-chain" || transport == "inline-observer-disable")
            QVERIFY(QFile::copy(LITECODE_DEFAULT_DEBUG_PLUGIN, directory.filePath(QFileInfo(LITECODE_DEFAULT_DEBUG_PLUGIN).fileName())));
        QTcpServer tcp; QLocalServer pipe;
        std::unique_ptr<SocketAdapterFixture> peer;
        connect(&tcp, &QTcpServer::newConnection, this, [&] { peer = std::make_unique<SocketAdapterFixture>(tcp.nextPendingConnection()); });
        connect(&pipe, &QLocalServer::newConnection, this, [&] { peer = std::make_unique<SocketAdapterFixture>(pipe.nextPendingConnection()); });
        QJsonObject configuration{{"name", "Native provider"}, {"type", "fixture"}, {"request", "launch"}};
        if (transport == "inline-start-terminal") {
            configuration.insert("terminalFixture", true);
            configuration.insert("terminalArguments", QJsonObject{{"kind", "integrated"}, {"title", "Fixture Debug Terminal"},
                {"cwd", directory.path()}, {"args", QJsonArray{LITECODE_DAP_ADAPTER_FIXTURE, "--terminal-target", "space value"}},
                {"env", QJsonObject{{"LITECODE_DEBUG_TEST", "fixture-value"}}}});
        }
        if (transport.startsWith("inline-start-")) {
            configuration.insert("providerStages", true); configuration.insert("asyncProvider", true);
            if (transport != "inline-start-no-folder") configuration.insert("program", "${workspaceFolder}/sample.py");
            if (transport == "inline-start-canceled") configuration.insert("cancelAs", "undefined");
            if (transport == "inline-start-failed") configuration.insert("preLaunchTask", "missing-task");
        }
        if (transport.startsWith("executable")) {
            configuration.insert("adapterForTest", LITECODE_DAP_ADAPTER_FIXTURE); configuration.insert("fixtureSession", true);
        } else if (transport == "tcp") {
            QVERIFY(tcp.listen(QHostAddress::LocalHost, 0)); configuration.insert("serverPort", tcp.serverPort());
        } else if (transport.startsWith("inline")) {
            configuration.insert("inlineFixture", true);
            if (transport == "inline-prelaunch-failure") configuration.insert("preLaunchTask", "missing-task");
            if (transport == "inline-config-chain") {
                configuration.insert("trackerFixtureMode", "async");
                configuration.insert("providerStages", true); configuration.insert("asyncProvider", true);
                configuration.insert("program", "${workspaceFolder}/sample.py");
            }
            if (transport.startsWith("inline-tracker-")) configuration.insert("trackerFixtureMode", transport.mid(15));
        } else {
            QVERIFY(pipe.listen(directory.filePath("adapter-pipe"))); configuration.insert("pipePath", pipe.fullServerName());
        }
        QVERIFY(QDir(directory.path()).mkdir(".vscode")); QFile launch(directory.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        launch.write(QJsonDocument(QJsonObject{{"version", "0.2.0"}, {"configurations", QJsonArray{configuration}}}).toJson()); launch.close();
        extensions::ExtensionManager manager(directory.path()); manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(directory.path()); manager.discover(); QVERIFY(manager.setEnabled("tests.native", true));
        if (transport == "inline-config-chain" || transport == "inline-observer-disable") QVERIFY(manager.setEnabled("tests.defaultDebug", true));
        ui::RunDebugPanel panel; panel.setWorkspace(directory.path());
        ui::TerminalPanel terminal;
        terminal.resize(1000, 400);
        ui::DebugTerminalController terminalController(&panel, &terminal, [&] { return directory.path(); });
        connect(&terminalController, &ui::DebugTerminalController::terminalCreated, &terminal, [&] { terminal.show(); });
        panel.setIntegratedTerminalAvailable(true);
        QSignalSpy terminalReady(&terminal, &ui::TerminalPanel::debugCommandReady);
        manager.setDebugStartHandler([&panel](const QJsonValue& folder, const QJsonValue& config,
            const QJsonObject& options, extensions::CancellationToken token) {
            return panel.startFromExtension(folder, config, options, token);
        });
        connect(&panel, &ui::RunDebugPanel::debugSessionEvent, &manager, &extensions::ExtensionManager::publishDebugSessionEvent);
        connect(&panel, &ui::RunDebugPanel::debugTrackerEvent, &manager, &extensions::ExtensionManager::publishDebugTrackerEvent);
        connect(&manager, &extensions::ExtensionManager::debugSessionOwnerUnavailable, &panel, &ui::RunDebugPanel::cancelOwnedDebugSession);
        manager.setDebugSessionHandler([&panel](const QString& id, const QString& operation, const QString& command, const QJsonValue& args) {
            return panel.handleDebugSessionRequest(id, operation, command, args);
        });
        QHash<quint64, quint64> configurations;
        connect(&panel, &ui::RunDebugPanel::debugConfigurationRequested, &manager,
            [&](quint64 local, const QString& type, const QString& method, const QJsonObject& values) {
                const auto id = manager.requestDebugConfiguration(type, method, values,
                    extensions::DebugConfigurationProviderTriggerKind::Initial, panel.launchFolder()); QVERIFY(id); configurations.insert(id, local);
            });
        connect(&manager, &extensions::ExtensionManager::debugConfigurationResult, &panel,
            [&](quint64 id, const QJsonValue& values, const QString& error) {
                QVERIFY(configurations.contains(id)); QVERIFY2(error.isEmpty(), qPrintable(error));
                panel.completeDebugConfigurationRequest(configurations.take(id), values, error);
            });
        connect(&panel, &ui::RunDebugPanel::debugConfigurationCanceled, &manager, [&](quint64 local) {
            for (auto it = configurations.begin(); it != configurations.end(); ++it) {
                if (it.value() != local) continue;
                const auto id = it.key(); configurations.erase(it); manager.cancelDebugConfiguration(id); break;
            }
        });
        QHash<quint64, quint64> requests;
        connect(&panel, &ui::RunDebugPanel::debugAdapterReleased, &manager, [&](quint64 local) {
            for (auto it = requests.begin(); it != requests.end(); ++it) {
                if (it.value() != local) continue;
                const auto host = it.key(); requests.erase(it); manager.releaseDebugAdapter(host); break;
            }
        });
        connect(&panel, &ui::RunDebugPanel::debugAdapterRequested, &manager,
            [&](quint64 local, const QString& type, const QJsonObject& config, const QString& session) {
                const auto id = manager.requestDebugAdapter(type, config, session, panel.launchFolder()); QVERIFY(id); requests.insert(id, local);
            });
        connect(&manager, &extensions::ExtensionManager::debugAdapterResult, &panel,
            [&](quint64 id, const QJsonObject& descriptor, const QString& error) {
                QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(requests.contains(id));
                panel.completeDebugAdapterRequest(requests.value(id), descriptor, error);
            });
        panel.toggleBreakpoint(directory.filePath("sample.py"), 8);
        QSignalSpy released(&panel, &ui::RunDebugPanel::debugAdapterReleased);
        if (transport.startsWith("inline-start-")) {
            QSignalSpy started(&manager, &extensions::ExtensionManager::commandResult);
            auto requestedDirectory = directory.path();
            if (transport == "inline-start-other-folder") {
                QVERIFY(QDir(directory.path()).mkdir("requested-folder"));
                requestedDirectory = directory.filePath("requested-folder");
            }
            const QJsonValue choice = transport == "inline-start-named" ? QJsonValue(configuration.value("name")) : QJsonValue(configuration);
            const QJsonValue folder = transport == "inline-start-no-folder" ? QJsonValue(QJsonValue::Null)
                : QJsonValue(QUrl::fromLocalFile(requestedDirectory).toString());
            QVERIFY(manager.requestCommand("tests.native.debugStart", {choice, folder,
                QJsonObject{{"noDebug", transport == "inline-start-no-debug"}, {"suppressDebugToolbar", true}}}));
            QTRY_COMPARE_WITH_TIMEOUT(started.size(), 1, 15000);
            if (transport == "inline-start-canceled" || transport == "inline-start-failed") {
                if (transport == "inline-start-canceled") {
                    QVERIFY(started[0][2].toString().isEmpty()); QCOMPARE(started[0][1].value<QJsonValue>(), QJsonValue(false));
                } else QVERIFY(!started[0][2].toString().isEmpty());
                QVERIFY(manager.setEnabled("tests.native", false)); return;
            }
            QVERIFY2(started[0][2].toString().isEmpty(), qPrintable(started[0][2].toString()));
            QCOMPARE(started[0][1].value<QJsonValue>(), QJsonValue(true));
            QVERIFY(manager.requestCommand("tests.native.debugFactoryContext")); QTRY_COMPARE(started.size(), 2);
            const auto context = started.last()[1].value<QJsonValue>().toObject();
            const auto folderSeen = context.value("workspaceFolder");
            if (transport == "inline-start-no-folder") QVERIFY(folderSeen.isUndefined());
            else QCOMPARE(folderSeen.toObject().value("uri").toString(), folder.toString());
            if (transport == "inline-start-other-folder")
                QCOMPARE(context.value("configuration").toObject().value("program").toString(), requestedDirectory + "/sample.py");
            if (transport == "inline-start-no-debug") QVERIFY(context.value("configuration").toObject().value("noDebug").toBool());
        } else panel.startDebugging();
        auto* notice = panel.findChild<QLabel*>("runDebugNotice"); QVERIFY(notice);
        if (transport == "inline-start-terminal") {
            QTRY_COMPARE(terminalReady.size(), 1);
            QVERIFY2(terminalReady.first()[2].toString().isEmpty(), qPrintable(terminalReady.first()[2].toString()));
            QVERIFY(terminalReady.first()[1].toLongLong() > 0);
            auto* view = terminal.findChild<ui::TerminalView*>(); QVERIFY(view);
            QTRY_VERIFY_WITH_TIMEOUT(view->plainText().contains("TERMINAL_READY"), 10000);
            QVERIFY(view->plainText().contains("ARG:space value"));
            QVERIFY(view->plainText().contains("ENV:fixture-value"));
            QSignalSpy commands(&manager, &extensions::ExtensionManager::commandResult);
            QVERIFY(manager.requestCommand("tests.native.debugSessionRequest", {"fixtureTerminalResponse"}));
            QTRY_COMPARE(commands.size(), 1); QVERIFY(commands.first()[2].toString().isEmpty());
            const auto response = commands.first()[1].value<QJsonValue>().toObject();
            QVERIFY(response.value("success").toBool());
            QCOMPARE(response.value("body").toObject().value("shellProcessId").toDouble(), terminalReady.first()[1].toDouble());
            QTest::keyClicks(view, "hello"); QTest::keyClick(view, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(view->plainText().contains("TERMINAL_INPUT:hello"), 10000);
            QTest::keyClicks(view, "echo SHELL_SURVIVES_TARGET"); QTest::keyClick(view, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(view->plainText().count("SHELL_SURVIVES_TARGET") >= 2, 10000);
        }
        if (transport == "inline-prelaunch-failure") {
            QTRY_COMPARE_WITH_TIMEOUT(released.size(), 1, 15000);
            QVERIFY(!notice->text().isEmpty());
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(notice->text(), QString("Debug state: 4"), 15000);
            panel.pauseDebugging(); QTRY_VERIFY(panel.isPaused());
            if (transport == "inline-start-no-debug") panel.toggleBreakpoint(directory.filePath("sample.py"), 9);
            panel.evaluateConsole("answer"); QTRY_VERIFY(!panel.outputText().isEmpty());
            if (transport == "executable-owner-disabled") {
                QVERIFY(manager.setEnabled("tests.native", false));
                QTRY_COMPARE(notice->text(), QString("Debug state: 0"));
            }
            if (transport == "inline-observer-disable") {
                QSignalSpy observed(&manager, &extensions::ExtensionManager::commandResult);
                QVERIFY(manager.requestCommand("tests.defaultDebug.trackerContext")); QTRY_COMPARE(observed.size(), 1);
                QVERIFY(observed[0][2].toString().isEmpty());
                QCOMPARE(observed[0][1].value<QJsonValue>().toObject().value("starts").toInt(), 1);
                QVERIFY(manager.setEnabled("tests.defaultDebug", false));
                QVERIFY(panel.isPaused());
                panel.evaluateConsole("after observer shutdown");
            }
            if (transport == "inline-tracker-error") {
                QSignalSpy failed(&manager, &extensions::ExtensionManager::commandResult);
                QVERIFY(manager.requestCommand("tests.native.debugSessionRequest", QJsonArray{"fixtureInvalid"}));
                QTRY_COMPARE(failed.size(), 1); QVERIFY(!failed[0][2].toString().isEmpty());
                QTRY_COMPARE(notice->text(), QString("Unknown DAP message type."));
            }
            if (transport == "inline-config-chain") {
                QSignalSpy commands(&manager, &extensions::ExtensionManager::commandResult);
                QVERIFY(manager.requestCommand("tests.native.debugSessionContext")); QTRY_COMPARE(commands.size(), 1);
                auto context = commands.last()[1].value<QJsonValue>().toObject();
                QVERIFY(context.value("active").toBool()); QVERIFY(context.value("sameIdentity").toBool());
                QCOMPARE(context.value("started").toInt(), 1);
                QVERIFY(manager.requestCommand("tests.native.debugSessionRename", QJsonArray{"Renamed session"}));
                QTRY_COMPARE(commands.size(), 2); QVERIFY(commands.last()[2].toString().isEmpty());
                const QJsonArray payload{"value", 42, QJsonValue(QJsonValue::Null)};
                QVERIFY(manager.requestCommand("tests.native.debugSessionRequest", QJsonArray{"fixtureEcho", payload}));
                QTRY_COMPARE(commands.size(), 3); QVERIFY2(commands.last()[2].toString().isEmpty(), qPrintable(commands.last()[2].toString()));
                QCOMPARE(commands.last()[1].value<QJsonValue>().toArray(), payload);
                QVERIFY(manager.requestCommand("tests.native.debugSessionContext")); QTRY_COMPARE(commands.size(), 4);
                context = commands.last()[1].value<QJsonValue>().toObject();
                QCOMPARE(context.value("name").toString(), QString("Renamed session"));
                QCOMPARE(context.value("customEvent").toString(), QString("fixtureCustom"));
                QCOMPARE(context.value("customBody").toArray(), payload);
                const auto hanging = manager.requestCommand("tests.native.debugSessionRequest", QJsonArray{"fixtureHang"}); QVERIFY(hanging);
                QTest::qWait(100);
                const auto stopping = manager.requestCommand("tests.native.debugSessionStop"); QVERIFY(stopping);
                QTRY_COMPARE(commands.size(), 6);
                bool sawHang{}, sawStop{};
                for (const auto& response : commands) {
                    if (response[0].toULongLong() == hanging) { sawHang = true; QVERIFY(!response[2].toString().isEmpty()); }
                    if (response[0].toULongLong() == stopping) { sawStop = true; QVERIFY2(response[2].toString().isEmpty(), qPrintable(response[2].toString())); }
                }
                QVERIFY(sawHang); QVERIFY(sawStop);
                QTRY_COMPARE(notice->text(), QString("Debug state: 0"));
                QVERIFY(manager.requestCommand("tests.native.debugSessionContext")); QTRY_COMPARE(commands.size(), 7);
                context = commands.last()[1].value<QJsonValue>().toObject();
                QVERIFY(!context.value("active").toBool()); QCOMPARE(context.value("terminated").toInt(), 1);
            }
            panel.stop(); QTRY_COMPARE(notice->text(), transport == "inline-tracker-error" ? QString("Unknown DAP message type.") : QString("Debug state: 0"));
        }
        if (transport.startsWith("inline")) {
            QSignalSpy commands(&manager, &extensions::ExtensionManager::commandResult);
            QVERIFY(manager.requestCommand("tests.native.debugFactoryContext")); QTRY_COMPARE(commands.size(), 1);
            QVERIFY(commands[0][2].toString().isEmpty());
            QCOMPARE(commands[0][1].value<QJsonValue>().toObject().value("inlineDisposed").toInt(), 1);
            if (transport == "inline-config-chain") {
                const auto values = commands[0][1].value<QJsonValue>().toObject().value("configuration").toObject();
                QCOMPARE(values.value("stages").toArray(), (QJsonArray{"before:default", "before:native", "after:default", "after:native"}));
                QCOMPARE(values.value("program").toString(), directory.path() + "/sample.py");
            }
        }
        if (transport == "tcp") QVERIFY(tcp.isListening()); else if (transport == "pipe") QVERIFY(pipe.isListening());
        if (transport == "executable-owner-disabled") return;
        QSignalSpy trackerResults(&manager, &extensions::ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.debugTrackerContext")); QTRY_COMPARE(trackerResults.size(), 1);
        QVERIFY(trackerResults[0][2].toString().isEmpty());
        const auto trackers = trackerResults[0][1].value<QJsonValue>().toObject();
        for (const auto& type : {QString("typed"), QString("wildcard")}) {
            const auto state = trackers.value(type).toObject(); QCOMPARE(state.value("factoryCalls").toInt(), 1);
            const bool tracked = transport != "inline-prelaunch-failure" &&
                (!transport.startsWith("inline-tracker-") || transport == "inline-tracker-error");
            QCOMPARE(state.value("starts").toInt(), tracked ? 1 : 0);
            QCOMPARE(state.value("stops").toInt(), tracked ? 1 : 0);
            QCOMPARE(state.value("exits").toInt(), tracked ? 1 : 0);
            if (tracked) {
                QVERIFY(state.value("receives").toInt() > 0); QVERIFY(state.value("sends").toInt() > 0);
                QVERIFY(state.value("commands").toArray().contains("initialize"));
                if (transport == "inline-start-no-debug") QVERIFY(!state.value("commands").toArray().contains("setBreakpoints"));
                if (transport == "inline-tracker-error") QVERIFY(state.value("errors").toInt() >= 1);
                else QVERIFY(state.value("commands").toArray().contains("disconnect"));
                if (transport == "executable") QCOMPARE(state.value("exitCode").toInt(-100), 0);
                else QVERIFY(!state.contains("exitCode"));
            }
        }
        if (transport == "inline-config-chain") {
            QVERIFY(manager.requestCommand("tests.defaultDebug.trackerContext")); QTRY_COMPARE(trackerResults.size(), 2);
            QVERIFY(trackerResults.last()[2].toString().isEmpty());
            const auto state = trackerResults.last()[1].value<QJsonValue>().toObject();
            QCOMPARE(state.value("starts").toInt(), 1); QCOMPARE(state.value("exits").toInt(), 1);
            QVERIFY(state.value("commands").toArray().contains("fixtureEcho"));
        }
        QVERIFY(manager.setEnabled("tests.native", false));
    }
};
QTEST_MAIN(ExtensionDebugTests)
#include "tst_extension_debug.moc"
