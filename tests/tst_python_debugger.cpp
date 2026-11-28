#include "PythonConfiguration.h"
#include "PythonEnvironment.h"
#include "extensions/ExtensionManager.h"
#include "extensions/NativeExtensionInstaller.h"
#include "ui/QuickOpenDialog.h"
#include "ui/RunDebugPanel.h"
#include "ui/TerminalPanel.h"
#include "ui/TerminalView.h"
#include "ui/debug/DebugConfigurationPicker.h"
#include "ui/debug/DebugTerminalController.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>
using namespace litecode;
class PythonDebuggerTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void configurationValidationAndEnvironment() {
        QTemporaryDir folder;
        QFile script(folder.filePath("sample.py"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("print('hello')\n");
        script.close();
        auto config = python::preparePythonConfiguration(
            {{"type", "debugpy"}, {"request", "launch"}, {"program", script.fileName()}},
            folder.path(), script.fileName(), true, {});
        QCOMPARE(config.value("console"), QJsonValue("integratedTerminal"));
        QVERIFY(config.value("justMyCode").toBool());
        python::validatePythonConfiguration(config);
        config.insert("args", QJsonArray{"space in argument", ""});
        python::validatePythonConfiguration(config);
        config.insert("args", "bad arguments");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, python::validatePythonConfiguration(config));
        config.remove("args");
        config.insert("module", "also.a.module");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, python::validatePythonConfiguration(config));
        for (const auto value :
             {QJsonValue(0), QJsonValue(65536), QJsonValue(1.5), QJsonValue("invalid")}) {
            QJsonObject attach{{"type", "debugpy"},
                               {"request", "attach"},
                               {"connect", QJsonObject{{"port", value}}}};
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                     python::validatePythonConfiguration(attach));
        }
        for (const auto value :
             {QJsonValue(true), QJsonValue(1.5), QJsonValue("-1"), QJsonValue("invalid")}) {
            const QJsonObject attach{
                {"type", "debugpy"}, {"request", "attach"}, {"processId", value}};
            QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                     python::validatePythonConfiguration(attach));
        }
        QFile env(folder.filePath(".env"));
        QVERIFY(env.open(QIODevice::WriteOnly));
        env.write("BASE=from-file\nJOINED=\"${BASE}/child\"\nQUOTED='literal "
                  "${BASE}'\nMULTI=\"line1\\nline2\"\n");
        env.close();
        const auto values = python::pythonEnvironment(
            env.fileName(), {{"BASE", "override"}, {"REMOVE", QJsonValue::Null}});
        QCOMPARE(values.value("BASE"), QJsonValue("override"));
        QCOMPARE(values.value("JOINED"), QJsonValue("override/child"));
        QCOMPARE(values.value("QUOTED"), QJsonValue("literal ${BASE}"));
        QCOMPARE(values.value("MULTI"), QJsonValue("line1\nline2"));
        QVERIFY(values.value("REMOVE").isNull());
    }
    void firstRunSelectsDebuggerWithoutCreatingLaunchJson() {
        QTemporaryDir extensions, workspace;
        const auto installed =
            extensions::installNativeExtension(LITECODE_PYTHON_PACKAGE, extensions.path());
        QVERIFY2(installed.error.isEmpty(), qPrintable(installed.error));
        extensions::ExtensionManager manager(extensions.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(workspace.path());
        manager.discover();
        QVERIFY(manager.setEnabled("litecode.python-debugger", true));
        QVERIFY(manager.breakpointLanguages().contains("python"));
        QVERIFY(!manager.breakpointLanguages().contains("json"));
        QWidget window;
        window.resize(900, 600);
        window.show();
        ui::DebugConfigurationPicker flow(&manager, [&] { return workspace.path(); }, &window);
        QSignalSpy selected(&flow, &ui::DebugConfigurationPicker::configurationSelected);
        QSignalSpy openLaunch(&flow, &ui::DebugConfigurationPicker::openConfiguration);
        const auto visiblePicker = [&]() -> ui::QuickOpenDialog* {
            for (auto* picker : window.findChildren<ui::QuickOpenDialog*>())
                if (picker->isVisible())
                    return picker;
            return nullptr;
        };
        flow.selectAndStart();
        QTRY_VERIFY(visiblePicker());
        auto* debugger = visiblePicker();
        QCOMPARE(debugger->windowTitle(), QString("Select a debugger"));
        auto* query = debugger->findChild<QLineEdit*>();
        QVERIFY(query);
        QCOMPARE(query->placeholderText(), QString("Debugger"));
        QTest::keyClick(query, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(visiblePicker(), 10000);
        auto* configuration = visiblePicker();
        QCOMPARE(configuration->windowTitle(), QString("Select a debug configuration"));
        query = configuration->findChild<QLineEdit*>();
        QVERIFY(query);
        QTest::keyClick(query, Qt::Key_Return);
        QTRY_COMPARE(selected.size(), 1);
        const auto values = selected.first().at(0).toJsonObject();
        QCOMPARE(values.value("type"), QJsonValue("debugpy"));
        QCOMPARE(values.value("program"), QJsonValue("${file}"));
        QCOMPARE(openLaunch.size(), 0);
        QVERIFY(!QFileInfo::exists(workspace.filePath(".vscode/launch.json")));
        QVERIFY(manager.setEnabled("litecode.python-debugger", false));
    }
    void packagedExtensionDebugsRealPython_data() {
        QTest::addColumn<QString>("console");
        QTest::addColumn<bool>("module");
        QTest::addColumn<bool>("noDebug");
        QTest::addColumn<bool>("remote");
        QTest::newRow("internal-file") << QString("internalConsole") << false << false << false;
        QTest::newRow("integrated-file")
            << QString("integratedTerminal") << false << false << false;
        QTest::newRow("module-no-debug") << QString("internalConsole") << true << true << false;
        QTest::newRow("remote-attach") << QString("internalConsole") << false << false << true;
    }
    void packagedExtensionDebugsRealPython() {
        QFETCH(QString, console);
        QFETCH(bool, module);
        QFETCH(bool, noDebug);
        QFETCH(bool, remote);
        if (QString(LITECODE_TEST_PYTHON).isEmpty())
            QSKIP("Python 3.10+ is needed for the real debugpy workflow.");
        QTemporaryDir extensions, workspace;
        const auto installed =
            extensions::installNativeExtension(LITECODE_PYTHON_PACKAGE, extensions.path());
        QVERIFY2(installed.error.isEmpty(), qPrintable(installed.error));
        QFile script(workspace.filePath("sample.py"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("import sys, os\ndef total(values):\n    result = sum(values)\n    return "
                     "result\nanswer = total([2, 3, 5])\nprint('PYTHON_DEBUGGER_OK', answer, "
                     "sys.argv[1], os.getenv('CHECK_ENV'))\n");
        script.close();
        QFile env(workspace.filePath(".env"));
        QVERIFY(env.open(QIODevice::WriteOnly));
        env.write("CHECK_ENV=from-file\n");
        env.close();
        extensions::ExtensionManager manager(extensions.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(workspace.path());
        manager.discover();
        QVERIFY2(manager.extensions().size() == 1, "The isolated package was not discovered.");
        QVERIFY(manager.setEnabled("litecode.python-debugger", true));
        QVERIFY(manager.breakpointLanguages().contains("python"));
        QVERIFY(!manager.breakpointLanguages().contains("json"));
        ui::RunDebugPanel panel;
        panel.setWorkspace(workspace.path());
        panel.setIntegratedTerminalAvailable(true);
        panel.setBreakpointLanguages(manager.breakpointLanguages());
        QHash<QString, QJsonObject> schemas;
        for (const auto& contribution : manager.debuggerContributions())
            schemas.insert(contribution.type, contribution.configurationAttributes);
        panel.setDebuggerSchemas(schemas);
        ui::TerminalPanel terminal;
        terminal.resize(900, 300);
        terminal.setWorkingDirectory(QDir::tempPath());
        if (console == "integratedTerminal") {
            terminal.show();
            QTest::qWait(100);
        }
        const auto initialTerminalViews = terminal.findChildren<ui::TerminalView*>().size();
        ui::DebugTerminalController terminals(&panel, &terminal, [&] { return workspace.path(); });
        QVector<core::OperationId> terminalIds;
        connect(&terminal, &ui::TerminalPanel::debugCommandReady, &panel,
                [&](core::OperationId id, qint64, const QString&) { terminalIds.append(id); });
        const bool measure = qEnvironmentVariableIsSet("LITECODE_DEBUG_STARTUP_TIMING");
        QElapsedTimer startupTimer;
        startupTimer.start();
        const auto timing =
            [&](const QString& stage) {
                if (measure)
                    qInfo().noquote()
                        << QString("startup.%1=%2ms").arg(stage).arg(startupTimer.elapsed());
            };
        connect(
            &terminal, &ui::TerminalPanel::debugCommandReady, &panel,
            [&](core::OperationId, qint64, const QString&) { timing("terminal.commandReady"); });
        connect(&panel, &ui::RunDebugPanel::debugTrackerEvent, &panel,
                [&](const QString&, const QJsonObject& event) {
                    const auto message = event.value("message").toObject();
                    const auto command = message.value("command").toString();
                    if (message.value("type") == "request" &&
                        (command == "initialize" || command == "launch" ||
                         command == "runInTerminal" || command == "configurationDone"))
                        timing(command);
                });
        QHash<quint64, quint64> configurations, adapters;
        QString failure;
        QJsonObject adapterDescriptor;
        connect(&panel, &ui::RunDebugPanel::debugConfigurationRequested, &manager,
                [&](quint64 local, const QString& type, const QString& method,
                    const QJsonObject& values) {
                    timing(method + ".request");
                    const auto request = manager.requestDebugConfiguration(type, method, values);
                    configurations.insert(request, local);
                });
        connect(&manager, &extensions::ExtensionManager::debugConfigurationResult, &panel,
                [&](quint64 request, const QJsonValue& values, const QString& error) {
                    if (!error.isEmpty())
                        failure = error;
                    timing("configuration.result");
                    panel.completeDebugConfigurationRequest(configurations.take(request), values,
                                                            error);
                });
        connect(&panel, &ui::RunDebugPanel::debugAdapterRequested, &manager,
                [&](quint64 local, const QString& type, const QJsonObject& config,
                    const QString& session) {
                    timing("adapter.request");
                    const auto request = manager.requestDebugAdapter(type, config, session);
                    adapters.insert(request, local);
                });
        connect(&manager, &extensions::ExtensionManager::debugAdapterResult, &panel,
                [&](quint64 request, const QJsonObject& descriptor, const QString& error) {
                    if (!error.isEmpty())
                        failure = error;
                    timing("adapter.result");
                    adapterDescriptor = descriptor;
                    panel.completeDebugAdapterRequest(adapters.take(request), descriptor, error);
                });
        QJsonObject configuration{{"name", "Python integration"},
                                  {"type", "debugpy"},
                                  {"request", "launch"},
                                  {"python", LITECODE_TEST_PYTHON},
                                  {"console", console},
                                  {"cwd", workspace.path()},
                                  {"args", QJsonArray{"space in argument"}},
                                  {"envFile", env.fileName()},
                                  {"env", QJsonObject{{"CHECK_ENV", "override"}}},
                                  {"noDebug", noDebug}};
        if (measure)
            configuration.remove("python");
        configuration.insert(module ? "module" : "program",
                             module ? QString("sample") : script.fileName());
        QProcess target;
        QString remoteOutput;
        if (remote) {
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("CHECK_ENV", "override");
            target.setProcessEnvironment(environment);
            target.setWorkingDirectory(workspace.path());
            target.start(
                LITECODE_TEST_PYTHON,
                {"-u", "-c",
                 "import sys,runpy; sys.path.insert(0,sys.argv[2]); import debugpy; "
                 "endpoint=debugpy.listen(('127.0.0.1',0)); print(endpoint[1],flush=True); "
                 "debugpy.wait_for_client(); program=sys.argv[1]; sys.argv=[program,'space in "
                 "argument']; "
                 "runpy.run_path(program,run_name='__main__')",
                 script.fileName(),
                 QDir(QFileInfo(installed.path).absolutePath()).filePath("bundled/libs")});
            QVERIFY(target.waitForStarted());
            QVERIFY2(QTest::qWaitFor([&] { return target.canReadLine(); }, 10000),
                     qPrintable(target.readAllStandardError()));
            bool validPort = false;
            const auto port = target.readLine().trimmed().toInt(&validPort);
            QVERIFY(validPort);
            connect(&target, &QProcess::readyReadStandardOutput, &panel,
                    [&] { remoteOutput += target.readAllStandardOutput(); });
            configuration = {{"name", "Remote Python"},
                             {"type", "debugpy"},
                             {"request", "attach"},
                             {"connect", QJsonObject{{"host", "127.0.0.1"}, {"port", port}}},
                             {"python", "nonexistent-python-not-needed-for-connect"}};
        }
        if (!noDebug)
            panel.toggleBreakpoint(script.fileName(), 4);
        if (noDebug) {
            QDir().mkpath(workspace.filePath(".vscode"));
            QFile launch(workspace.filePath(".vscode/launch.json"));
            QVERIFY(launch.open(QIODevice::WriteOnly));
            configuration.remove("noDebug");
            launch.write(QJsonDocument(QJsonObject{{"version", "0.2.0"},
                                                   {"configurations", QJsonArray{configuration}}})
                             .toJson());
            launch.close();
            panel.setWorkspace(workspace.path());
            panel.runWithoutDebugging();
        } else
            panel.startSelectedConfiguration(configuration);
        if (!noDebug) {
            QVERIFY2(QTest::qWaitFor([&] { return panel.isPaused() || !failure.isEmpty(); }, 20000),
                     qPrintable(panel.outputText()));
            QVERIFY2(failure.isEmpty(), qPrintable(failure));
            QVERIFY(panel.isPaused());
            timing("breakpoint");
            auto* variables = panel.findChild<QTreeWidget*>("debugVariables");
            QVERIFY(variables);
            QTRY_VERIFY(variables->topLevelItemCount() > 0);
            auto* breakpointRows = panel.findChild<QTreeWidget*>("debugBreakpoints");
            QVERIFY(breakpointRows);
            QVERIFY(breakpointRows->topLevelItemCount() >= 3);
            auto* exception = breakpointRows->topLevelItem(0);
            QVERIFY(exception->isFirstColumnSpanned());
            const auto checked = exception->checkState(0);
            if (console == "integratedTerminal") {
                panel.resize(360, 800);
                panel.show();
                QTest::qWait(50);
                auto* state = panel.findChild<QLabel*>("debugCallStackState");
                QVERIFY(state && state->isVisible() && state->width() > 50);
                const auto rectangle = breakpointRows->visualItemRect(exception);
                QTest::mouseClick(breakpointRows->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  QPoint(rectangle.left() + 27, rectangle.center().y()));
                QTRY_VERIFY(exception->checkState(0) != checked);
                exception->setCheckState(0, checked);
            }
            panel.evaluateConsole("result");
            QTRY_VERIFY_WITH_TIMEOUT(panel.outputText().contains("10"), 5000);
            if (console == "integratedTerminal")
                QVERIFY(panel.usesIntegratedTerminal());
            QSignalSpy locations(&panel, &ui::RunDebugPanel::pausedLocationChanged);
            panel.stepOver();
            QTRY_VERIFY_WITH_TIMEOUT(panel.isPaused() && !locations.isEmpty() &&
                                         locations.last().at(1).toInt() >= 5,
                                     5000);
            panel.continueDebugging();
        }
        QTRY_VERIFY_WITH_TIMEOUT(!adapterDescriptor.isEmpty() || !failure.isEmpty(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        if (remote)
            QCOMPARE(adapterDescriptor.value("type"), QJsonValue("server"));
        else {
            QVERIFY(adapterDescriptor.value("command").toString().contains("python",
                                                                           Qt::CaseInsensitive));
            QCOMPARE(QFileInfo(adapterDescriptor.value("args").toArray().last().toString())
                         .canonicalFilePath(),
                     QFileInfo(QDir(QFileInfo(installed.path).absolutePath())
                                   .filePath("bundled/libs/debugpy/adapter"))
                         .canonicalFilePath());
        }
        const auto programOutput = [&] {
            if (remote)
                return remoteOutput;
            if (console == "internalConsole")
                return panel.outputText();
            QString text;
            for (auto* view : terminal.findChildren<ui::TerminalView*>())
                text += view->plainText();
            return text;
        };
        QVERIFY2(QTest::qWaitFor(
                     [&] {
                         return programOutput().contains(
                                    "PYTHON_DEBUGGER_OK 10 space in argument override") ||
                                !failure.isEmpty();
                     },
                     20000),
                 qPrintable(programOutput() + panel.outputText()));
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(programOutput().contains("PYTHON_DEBUGGER_OK 10 space in argument override"));
        if (!remote)
            QTRY_VERIFY_WITH_TIMEOUT(panel.outputText().contains("Process exited with code 0"),
                                     10000);
        QVERIFY(!panel.outputText().contains("Debug adapter crashed"));
        QVERIFY2(!panel.outputText().contains("disconnected unexpectedly"),
                 qPrintable(panel.outputText()));
        panel.stop();
        if (remote) {
            QTRY_COMPARE_WITH_TIMEOUT(target.state(), QProcess::NotRunning, 5000);
            QCOMPARE(target.exitCode(), 0);
        }
        for (const auto id : terminalIds)
            terminal.stopProcess(id);
        if (!terminalIds.isEmpty())
            QTRY_COMPARE(terminal.findChildren<ui::TerminalView*>().size(), initialTerminalViews);
        QVERIFY(manager.setEnabled("litecode.python-debugger", false));
    }
};
QTEST_MAIN(PythonDebuggerTests)
#include "tst_python_debugger.moc"
