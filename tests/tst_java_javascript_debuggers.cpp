#include "extensions/ExtensionManager.h"
#include "extensions/NativeExtensionInstaller.h"
#include "ui/RunDebugPanel.h"
#include "ui/TerminalPanel.h"
#include "ui/TerminalView.h"
#include "ui/Theme.h"
#include "ui/debug/DebugConfigurationPicker.h"
#include "ui/debug/DebugTerminalController.h"
#include <QFile>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>
#include <algorithm>

using namespace litecode;

class PackagedDebuggerTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void welcomeKeepsBreakpointsVisibleWithoutAConfiguration() {
        ui::Theme::apply(*qApp, false);
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString file = workspace.filePath("example.js");
        ui::RunDebugPanel panel;
        panel.resize(340, 600);
        panel.setWorkspace(workspace.path());
        panel.toggleBreakpoint(file, 8);
        QSignalSpy start(&panel, &ui::RunDebugPanel::selectDebugConfigurationRequested);
        QSignalSpy configure(&panel, &ui::RunDebugPanel::configurationRequested);
        QSignalSpy openFile(&panel, &ui::RunDebugPanel::openFileRequested);
        panel.show();
        auto* run = panel.findChild<QPushButton*>("runDebugWelcomeButton");
        auto* breakpoints = panel.findChild<QTreeWidget*>("debugBreakpoints");
        auto* prompt = panel.findChild<QLabel*>("debugWelcomeOpenFile");
        auto* guidance = panel.findChild<QLabel*>("debugWelcomeConfiguration");
        QTRY_VERIFY(run && run->isVisible() && breakpoints && breakpoints->isVisible());
        QCOMPARE(breakpoints->topLevelItemCount(), 1);
        QVERIFY(!panel.findChild<QTreeWidget*>("debugVariables")->isVisible());
        QVERIFY(run->mapTo(&panel, QPoint{}).y() < panel.height() / 3);
        QVERIFY(breakpoints->mapTo(&panel, QPoint{}).y() > panel.height() / 2);
        QVERIFY(prompt && prompt->isVisible() && guidance && guidance->isVisible());
        emit prompt->linkActivated("open");
        emit guidance->linkActivated("launch");
        QTest::mouseClick(run, Qt::LeftButton);
        QCOMPARE(openFile.size(), 1);
        QCOMPARE(configure.size(), 1);
        QCOMPARE(start.size(), 1);
        if (const auto capture = qEnvironmentVariable("LITECODE_DEBUG_WELCOME_CAPTURE"); !capture.isEmpty())
            QVERIFY(panel.grab().save(capture));
        panel.setActiveFile(file);
        QVERIFY(!prompt->isVisible());
        panel.selectDynamicConfiguration(
            {{"name", "Current File"}, {"type", "node"}, {"request", "launch"}});
        QVERIFY(!run->isVisible());
        QVERIFY(panel.findChild<QTreeWidget*>("debugVariables")->isVisible());
        QVERIFY(breakpoints->isVisible());
        QCOMPARE(breakpoints->topLevelItemCount(), 1);
    }

    void configurationPickerOpensOnSingleClick() {
        QTemporaryDir workspace;
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        launch.write(R"({"configurations":[{"name":"Node.js","type":"node","request":"launch"},{"name":"Python","type":"debugpy","request":"launch"}]})");
        launch.close();
        ui::RunDebugPanel panel;
        panel.setWorkspace(workspace.path());
        panel.setDebuggerChoices({{"node", "Node.js"}, {"debugpy", "Python Debugger"}});
        extensions::ExtensionManager manager(workspace.filePath("extensions"));
        ui::DebugConfigurationPicker configurations(&manager, [&] { return workspace.path(); }, &panel);
        connect(&panel, &ui::RunDebugPanel::configurationRequested,
                &configurations, &ui::DebugConfigurationPicker::createLaunch);
        QSignalSpy openConfiguration(&configurations, &ui::DebugConfigurationPicker::openConfiguration);
        QSignalSpy providerRequested(&panel, &ui::RunDebugPanel::selectDynamicConfigurationRequested);
        panel.resize(360, 480);
        panel.show();
        auto* picker = panel.findChild<QComboBox*>("debugConfigurationPicker");
        QVERIFY(picker && picker->isVisible());
        auto* search = picker->findChild<QLineEdit*>("comboPopupSearch");
        QVERIFY(search);
        QElapsedTimer elapsed;
        elapsed.start();
        QTest::mouseClick(picker, Qt::LeftButton, Qt::NoModifier,
                          QPoint(picker->width() - 12, picker->height() / 2));
        QVERIFY(search->isVisible());
        qInfo() << "Configuration picker click to visible:" << elapsed.elapsed() << "ms";
        QTest::keyClicks(search, "Python");
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(picker->currentText(), QStringLiteral("Python"));
        QVERIFY(!search->isVisible());
        const auto clickAction = [picker](const QString& text) {
            QTest::keyClick(picker, Qt::Key_Space);
            for (auto* action : picker->findChildren<QPushButton*>()) {
                if (action->isVisible() && action->text() == text) {
                    QTest::mouseClick(action, Qt::LeftButton);
                    return true;
                }
            }
            return false;
        };
        QVERIFY(clickAction("Node.js..."));
        QCOMPARE(providerRequested.size(), 1);
        QCOMPARE(providerRequested.first().first().toString(), QStringLiteral("node"));
        QVERIFY(clickAction("Add Configuration..."));
        QCOMPARE(openConfiguration.size(), 1);
        QCOMPARE(openConfiguration.first().first().toString(), launch.fileName());
        QCOMPARE(picker->currentText(), QStringLiteral("Python"));
    }

    void launch_data() {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<int>("line");
        QTest::addColumn<bool>("integrated");
        const QByteArray js("const base = 21;\nconst answer = base * 2;\n"
                            "console.log('DEBUGGER_SMOKE', answer);\n");
        const QByteArray java(
            "public class Sample {\n    public static void main(String[] args) {\n"
            "        int base = 21;\n        int answer = base * 2;\n"
            "        System.out.println(\"DEBUGGER_SMOKE \" + answer);\n    }\n}\n");
        QTest::newRow("node") << QString("node") << QString("sample.js") << js << 2 << false;
        QTest::newRow("java") << QString("java") << QString("Sample.java") << java << 4 << false;
        QTest::newRow("node-terminal")
            << QString("node") << QString("sample.js") << js << 2 << true;
        QTest::newRow("java-terminal")
            << QString("java") << QString("Sample.java") << java << 4 << true;
    }

    void launch() {
        QFETCH(QString, type);
        QFETCH(QString, fileName);
        QFETCH(QByteArray, source);
        QFETCH(int, line);
        QFETCH(bool, integrated);
        const bool java = type == "java";
        const auto javaHome = qEnvironmentVariable("JAVA_HOME");
#ifdef Q_OS_WIN
        const auto javaName = QStringLiteral("bin/java.exe");
#else
        const auto javaName = QStringLiteral("bin/java");
#endif
        if (java && !QFileInfo(QDir(javaHome).filePath(javaName)).isExecutable() &&
            QStandardPaths::findExecutable("java").isEmpty())
            QSKIP("JDK 21 or newer is needed for the packaged Java debugger test.");
        if (!java && QStandardPaths::findExecutable("node").isEmpty())
            QSKIP("Node.js is needed for the packaged JavaScript debugger test.");
        QTemporaryDir installedExtensions, workspace;
        QVERIFY(installedExtensions.isValid() && workspace.isValid());
        const auto installed = extensions::installNativeExtension(
            java ? LITECODE_JAVA_PACKAGE : LITECODE_JS_PACKAGE, installedExtensions.path());
        QVERIFY2(installed.error.isEmpty(), qPrintable(installed.error));
        QFile program(workspace.filePath(fileName));
        QVERIFY(program.open(QIODevice::WriteOnly));
        QCOMPARE(program.write(source), source.size());
        program.close();

        extensions::ExtensionManager manager(installedExtensions.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(workspace.path());
        manager.discover();
        QVERIFY(manager.setEnabled(java ? "litecode.java-debugger" : "litecode.javascript-debugger",
                                   true));
        QVERIFY(manager.breakpointLanguages().contains(java ? "java" : "javascript"));

        ui::RunDebugPanel panel;
        panel.setWorkspace(workspace.path());
        auto* launchGroup = panel.findChild<QWidget*>("debugLaunchGroup");
        auto* sessionControls = panel.findChild<QWidget*>("debugSessionControls");
        QVERIFY(launchGroup && sessionControls);
        QVERIFY(!launchGroup->isHidden());
        QVERIFY(sessionControls->isHidden());
        ui::TerminalPanel terminal;
        terminal.setWorkingDirectory(QDir::tempPath());
        ui::DebugTerminalController terminals(&panel, &terminal, [&] { return workspace.path(); });
        panel.setIntegratedTerminalAvailable(integrated);
        if (integrated) {
            terminal.resize(900, 300);
            terminal.show();
        }
        panel.setBreakpointLanguages(manager.breakpointLanguages());
        QHash<QString, QJsonObject> schemas;
        for (const auto& contribution : manager.debuggerContributions())
            schemas.insert(contribution.type, contribution.configurationAttributes);
        panel.setDebuggerSchemas(schemas);

        QString failure;
        if (qEnvironmentVariableIsSet("LITECODE_TEST_DAP_TRACE")) {
            connect(&panel, &ui::RunDebugPanel::debugTrackerEvent, &panel,
                    [](const QString&, const QJsonObject& event) {
                        qInfo().noquote() << QJsonDocument(event).toJson(QJsonDocument::Compact);
                    });
        }
        QHash<quint64, quint64> configurations, adapters;
        connect(&panel, &ui::RunDebugPanel::debugConfigurationRequested, &manager,
                [&](quint64 id, const QString& debugType, const QString& method,
                    const QJsonObject& values) {
                    configurations.insert(
                        manager.requestDebugConfiguration(debugType, method, values), id);
                });
        connect(&manager, &extensions::ExtensionManager::debugConfigurationResult, &panel,
                [&](quint64 id, const QJsonValue& value, const QString& error) {
                    if (!error.isEmpty())
                        failure = error;
                    panel.completeDebugConfigurationRequest(configurations.take(id), value, error);
                });
        connect(&panel, &ui::RunDebugPanel::debugAdapterRequested, &manager,
                [&](quint64 id, const QString& debugType, const QJsonObject& config,
                    const QString& session) {
                    adapters.insert(manager.requestDebugAdapter(debugType, config, session), id);
                });
        connect(&manager, &extensions::ExtensionManager::debugAdapterResult, &panel,
                [&](quint64 id, const QJsonObject& descriptor, const QString& error) {
                    if (!error.isEmpty())
                        failure = error;
                    panel.completeDebugAdapterRequest(adapters.take(id), descriptor, error);
                });

        panel.toggleBreakpoint(program.fileName(), line);
        QJsonObject configuration{
            {"name", "Package smoke test"},
            {"type", type},
            {"request", "launch"},
            {"console", integrated ? "integratedTerminal" : "internalConsole"},
            {java ? "mainClass" : "program", program.fileName()}};
        QSignalSpy locations(&panel, &ui::RunDebugPanel::pausedLocationChanged);
        panel.startSelectedConfiguration(configuration);
        const auto report = [&] { return failure + '\n' + panel.outputText(); };
        QVERIFY2(QTest::qWaitFor([&] { return panel.isPaused() || !failure.isEmpty(); }, 45000),
                 qPrintable(report()));
        QVERIFY2(panel.isPaused(), qPrintable(report()));
        QVERIFY(launchGroup->isHidden());
        QVERIFY(!sessionControls->isHidden());
        const auto controlButtons = sessionControls->findChildren<QToolButton*>();
        QCOMPARE(controlButtons.size(), 5);
        for (const auto* button : controlButtons)
            QVERIFY(button->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!locations.isEmpty() && locations.last().at(1).toInt() == line,
                                 5000);
        QCOMPARE(locations.last().at(1).toInt(), line);
        if (!java && !integrated) {
            panel.resize(420, 760);
            panel.show();
            auto* variables = panel.findChild<QTreeWidget*>("debugVariables");
            QVERIFY(variables);
            QTRY_VERIFY_WITH_TIMEOUT(variables->topLevelItemCount() > 0, 5000);
            auto* scope = variables->topLevelItem(0);
            QVERIFY(scope->isExpanded());
            const auto scopeRect = variables->visualItemRect(scope);
            const QPoint rowClick(scopeRect.left() + 50, scopeRect.center().y());
            QTest::mouseClick(variables->viewport(), Qt::LeftButton, Qt::NoModifier, rowClick);
            QTRY_VERIFY(!scope->isExpanded());
            QTest::mouseClick(variables->viewport(), Qt::LeftButton, Qt::NoModifier, rowClick);
            QTRY_VERIFY(scope->isExpanded());
            const QPoint chevron(scopeRect.left() - variables->indentation() / 2,
                                scopeRect.center().y());
            QTest::mouseClick(variables->viewport(), Qt::LeftButton, Qt::NoModifier, chevron);
            QTRY_VERIFY(!scope->isExpanded());
            QTest::qWait(40);
            QVERIFY(!scope->isExpanded());

            auto* stack = panel.findChild<QTreeWidget*>("debugCallStack");
            QVERIFY(stack);
            auto* sections = panel.findChild<QWidget*>("debugSections");
            QVERIFY(sections);
            const int variablesInset = variables->mapTo(sections, QPoint{}).x();
            QVERIFY(variablesInset > 0);
            QCOMPARE(stack->mapTo(sections, QPoint{}).x(), variablesInset);
            QCOMPARE(stack->topLevelItemCount(), 1);
            auto* target = stack->topLevelItem(0);
            QCOMPARE(target->text(0), fileName);
            QVERIFY(target->isExpanded());
            QTreeWidgetItem* skipped = nullptr;
            int skippedRow = -1;
            for (int index = 0; index < target->childCount(); ++index) {
                if (target->child(index)->childCount() > 0) {
                    skipped = target->child(index);
                    skippedRow = index;
                    break;
                }
            }
            QVERIFY(skipped);
            QVERIFY(!skipped->isExpanded());
            QCOMPARE(skipped->childIndicatorPolicy(), QTreeWidgetItem::DontShowIndicator);
            const auto hiddenFrames = [&] {
                QList<QTreeWidgetItem*> items;
                for (int index = 0; index < skipped->childCount(); ++index)
                    items.append(skipped->child(index));
                return items;
            }();
            const int originalRows = target->childCount();
            stack->scrollToItem(skipped);
            const auto skippedRect = stack->visualItemRect(skipped);
            const QPoint skippedClick(skippedRect.left() + 50, skippedRect.center().y());
            QTest::mouseClick(stack->viewport(), Qt::LeftButton, Qt::NoModifier, skippedClick);
            QTRY_COMPARE(target->childCount(), originalRows - 1 + hiddenFrames.size());
            for (int index = 0; index < hiddenFrames.size(); ++index)
                QCOMPARE(target->child(skippedRow + index), hiddenFrames.at(index));

            auto* watch = panel.findChild<QTreeWidget*>("debugWatch");
            auto* addWatch = panel.findChild<QToolButton*>("debugAddWatchButton");
            QVERIFY(watch && addWatch);
            QTest::mouseClick(addWatch, Qt::LeftButton);
            auto* input = watch->findChild<QLineEdit*>("debugWatchExpressionInput");
            QVERIFY(input && input->isVisible());
            QCOMPARE(watch->itemWidget(watch->topLevelItem(0), 0), input);
            QTest::keyClicks(input, "base + 1");
            QTest::keyClick(input, Qt::Key_Return);
            QCOMPARE(watch->topLevelItemCount(), 1);
            QTRY_COMPARE(watch->topLevelItem(0)->text(1), QString("22"));

            QTest::mouseClick(addWatch, Qt::LeftButton);
            input = qobject_cast<QLineEdit*>(watch->itemWidget(watch->topLevelItem(1), 0));
            QVERIFY(input && input->isVisible());
            QTest::keyClicks(input, "base + 2");
            QTest::keyClick(input, Qt::Key_Escape);
            QCOMPARE(watch->topLevelItemCount(), 1);
        }
        panel.evaluateConsole("base");
        QVERIFY2(QTest::qWaitFor([&] { return panel.outputText().contains("21"); }, 5000),
                 qPrintable(report()));
        locations.clear();
        panel.stepOver();
        QTRY_VERIFY_WITH_TIMEOUT(!locations.isEmpty() && panel.isPaused() &&
                                     locations.last().at(1).toInt() == line + 1,
                                 5000);
        QCOMPARE(locations.last().at(1).toInt(), line + 1);
        panel.continueDebugging();
        const auto hasProgramOutput = [&] {
            if (!integrated)
                return panel.outputText().contains("DEBUGGER_SMOKE 42");
            for (auto* view : terminal.findChildren<ui::TerminalView*>()) {
                if (view->plainText().contains("DEBUGGER_SMOKE 42"))
                    return true;
            }
            return false;
        };
        QVERIFY2(QTest::qWaitFor(hasProgramOutput, 10000), qPrintable(report()));
        QTRY_VERIFY_WITH_TIMEOUT(!launchGroup->isHidden(), 5000);
        QVERIFY(sessionControls->isHidden());
#ifdef Q_OS_WIN
        if (!java && integrated) {
            const auto firstViews = terminal.findChildren<ui::TerminalView*>();
            const auto debugView = std::find_if(firstViews.cbegin(), firstViews.cend(), [](const auto* candidate) {
                return candidate->plainText().contains("DEBUGGER_SMOKE 42");
            });
            QVERIFY(debugView != firstViews.cend());
            auto* view = *debugView;
            QTRY_VERIFY_WITH_TIMEOUT(view->plainText().trimmed().endsWith('>'), 5000);
            panel.startSelectedConfiguration(configuration);
            QTRY_VERIFY_WITH_TIMEOUT(panel.isPaused() || !failure.isEmpty(), 10000);
            QVERIFY2(panel.isPaused(), qPrintable(report()));
            QCOMPARE(terminal.findChildren<ui::TerminalView*>(), firstViews);
            panel.continueDebugging();
            QTRY_VERIFY2_WITH_TIMEOUT(view->plainText().count("DEBUGGER_SMOKE 42") == 2,
                                      qPrintable(view->plainText()), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!launchGroup->isHidden(), 5000);
        }
#endif
    }
};

QTEST_MAIN(PackagedDebuggerTests)
#include "tst_java_javascript_debuggers.moc"
