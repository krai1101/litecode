#include "ui/components/WorkbenchTabs.h"
#include "core/SettingsService.h"
#include "editor/ScintillaEditor.h"
#include "ui/ConfirmationDialog.h"
#include "ui/DiffEditorWidget.h"
#include "ui/EditorArea.h"
#include "ui/ExtensionsPanel.h"
#include "ui/GitBranchDialog.h"
#include "ui/GitCredentialBroker.h"
#include "ui/GitHistoryWidget.h"
#include "ui/GitReferenceBadges.h"
#include "ui/MainWindow.h"
#include "ui/QuickOpenDialog.h"
#include "ui/RunDebugPanel.h"
#include "ui/SourceControlPanel.h"
#include "ui/TerminalPanel.h"
#include "ui/Theme.h"
#include "ui/WorkbenchChrome.h"
#include "ui/components/ComponentTokens.h"
#include "ui/components/Controls.h"
#include "ui/components/QuickInputDialog.h"
#include "ui/components/SidebarView.h"
#include "ui/controllers/WorkbenchController.h"
#include "workspace/GitService.h"
#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <ScintillaEditBase.h>
#include <ScintillaMessages.h>
#include <QtTest>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace litecode;
// UI regression driver only. Real native provider/host integration is covered
// separately by tst_extension_debug.cpp, not by a launch.json adapter override.
static void installTestAdapter(ui::RunDebugPanel& panel, const QString& command, const QStringList& arguments) {
    QObject::disconnect(&panel, &ui::RunDebugPanel::debugAdapterRequested, nullptr, nullptr);
    QJsonArray args; for (const auto& argument : arguments) args.append(argument);
    const QJsonObject descriptor{{"type", "executable"}, {"command", command}, {"args", args}};
    QObject::connect(&panel, &ui::RunDebugPanel::debugAdapterRequested, &panel,
        [&panel, descriptor](quint64 id, const QString&, const QJsonObject&, const QString&) {
            panel.completeDebugAdapterRequest(id, descriptor, {});
        });
}
class SourceControlUiTests : public QObject {
    Q_OBJECT
  private slots:
    void f5WithoutConfigurationShowsPicker() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        core::SettingsService settings(workspace.filePath("settings.ini"));
        workspace::WorkspaceService workspaceService;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspaceService, {search, quick, replace}, false);
        QString error;
        QVERIFY(window.openWorkspaceFolder(workspace.path(), &error));
        auto* action = window.findChild<QAction*>(QStringLiteral("debug.start"));
        QVERIFY(action);
        auto* panel = window.findChild<ui::RunDebugPanel*>();
        QVERIFY(panel);
        // Verify the command reaches the shared start flow, rather than opening
        // its own launch.json-only dialog. Provider picker coverage lives in
        // tst_python_debugger.cpp with a real installed debugger extension.
        QObject::disconnect(panel, &ui::RunDebugPanel::selectDebugConfigurationRequested,
                            nullptr, nullptr);
        QSignalSpy selectDebugger(panel, &ui::RunDebugPanel::selectDebugConfigurationRequested);
        QSignalSpy createLaunch(panel, &ui::RunDebugPanel::configurationRequested);
        action->trigger();
        QCOMPARE(selectDebugger.size(), 1);
        QCOMPARE(createLaunch.size(), 0);
        QVERIFY(!QFileInfo::exists(workspace.filePath(".vscode/launch.json")));
    }
    void runDebugSidebarShrinksToWorkbenchMinimum() {
        QTemporaryDir workspace;
        core::SettingsService settings(workspace.filePath("settings.ini"));
        workspace::WorkspaceService service;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, service, {search, quick, replace}, false);
        QString error;
        QVERIFY(window.openWorkspaceFolder(workspace.path(), &error));
        window.resize(1100, 720);
        window.show();
        auto* controller = window.findChild<ui::WorkbenchController*>();
        auto* dock = window.findChild<QDockWidget*>("workspaceDock");
        auto* panel = window.findChild<ui::RunDebugPanel*>();
        QVERIFY(controller && dock && panel);
        controller->showSidebar(ui::SidebarView::RunDebug);
        QTest::qWait(50);
        window.resizeDocks({dock}, {170}, Qt::Horizontal);
        QTRY_VERIFY2(dock->width() <= 172, qPrintable(QString("Sidebar width: %1, minimum hint: %2")
            .arg(dock->width()).arg(dock->minimumSizeHint().width())));
        QVERIFY(dock->width() >= 170);
        // A saved long configuration must not raise the minimum either.
        QDir().mkpath(workspace.filePath(".vscode"));
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        launch.write(R"({"version":"0.2.0","configurations":[{"name":"A very long debugger configuration name","type":"fixture","request":"launch"}]})");
        launch.close();
        panel->setWorkspace(workspace.path());
        window.resizeDocks({dock}, {170}, Qt::Horizontal);
        QTRY_VERIFY(dock->width() <= 172);
        auto* configure = window.findChild<QToolButton*>("debugConfigureButton");
        QVERIFY(configure && configure->isVisible());
        QVERIFY(configure->mapTo(dock, QPoint(configure->width(), 0)).x() <= dock->width());

        window.resizeDocks({dock}, {350}, Qt::Horizontal);
        QTRY_VERIFY(dock->width() >= 340);
        window.resizeDocks({dock}, {170}, Qt::Horizontal);
        QTRY_VERIFY(dock->width() <= 172);
    }
    void breakpointLanguagesExcludeLaunchJsonAndKeepPython() {
        QTemporaryDir folder;
        core::SettingsService settings(folder.filePath("settings.ini"));
        const auto json = folder.filePath("launch.json");
        const auto python = folder.filePath("example.py");
        settings.saveDebugBreakpoints(folder.path(), {{json, {7}}, {python, {9}}});
        ui::RunDebugPanel panel(nullptr, &settings);
        panel.setBreakpointLanguages({"python"});
        panel.setWorkspace(folder.path());
        auto* rows = panel.findChild<QTreeWidget*>("debugBreakpoints");
        QVERIFY(rows);
        QCOMPARE(rows->topLevelItemCount(), 1);
        QCOMPARE(rows->topLevelItem(0)->text(0), QString("example.py"));
        panel.toggleBreakpoint(json, 12);
        QVERIFY(!panel.hasBreakpoint(json, 12));
        panel.toggleBreakpoint(python, 10);
        QVERIFY(panel.hasBreakpoint(python, 10));
        QCOMPARE(rows->topLevelItemCount(), 2);
        // Unsupported saved entries are filtered, not silently destroyed.
        QVERIFY(settings.debugBreakpoints(folder.path()).value(json).contains(7));
    }
    void realDebugpySessionExitsWithoutAdapterCrashNotice() {
        const QString sample = qEnvironmentVariable("LITECODE_REAL_DEBUG_WORKSPACE");
        if (sample.isEmpty()) QSKIP("Set LITECODE_REAL_DEBUG_WORKSPACE to run debugpy integration.");
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        const QString source = workspace.filePath("example.py");
        QVERIFY(QFile::copy(QDir(sample).filePath("example.py"), source));
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        const QJsonObject configuration{
            {"name", "Python console test"}, {"type", "debugpy"}, {"request", "launch"},
            {"program", source}, {"cwd", workspace.path()},
            {"console", "internalConsole"}, {"redirectOutput", true}};
        launch.write(QJsonDocument(QJsonObject{{"version", "0.2.0"},
                                         {"configurations", QJsonArray{configuration}}}).toJson());
        launch.close();
        ui::Theme::apply(*qApp, false);
        ui::RunDebugPanel panel;
        panel.setWorkspace(workspace.path());
        QVERIFY(panel.hasConfigurations());
        panel.toggleBreakpoint(source, 8);
        installTestAdapter(panel, "python", {QDir(sample).filePath("debug_adapter.py")});
        panel.startDebugging();
        QTRY_VERIFY_WITH_TIMEOUT(panel.isPaused(), 30000);
        const QString capture = qEnvironmentVariable("LITECODE_REAL_DEBUG_CAPTURE");
        if (!capture.isEmpty()) {
            panel.resize(380, 900);
            panel.show();
            QTest::qWait(200);
            QVERIFY(panel.grab().save(capture));
        }
        panel.continueDebugging();
        QTRY_VERIFY_WITH_TIMEOUT(panel.outputText().contains(
            QStringLiteral("Process exited with code 0")), 30000);
        QTest::qWait(1000);
        QVERIFY2(!panel.outputText().contains(QStringLiteral("Debug adapter crashed")),
                 qPrintable(panel.outputText()));
    }
    void realDebugpyUsesIntegratedTerminal() {
        const QString sample = qEnvironmentVariable("LITECODE_REAL_DEBUG_WORKSPACE");
        if (sample.isEmpty()) QSKIP("Set LITECODE_REAL_DEBUG_WORKSPACE to run debugpy integration.");
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QVERIFY(QDir(folder.path()).mkdir(".vscode"));
        const QString source = folder.filePath("example.py");
        QVERIFY(QFile::copy(QDir(sample).filePath("example.py"), source));
        QFile launch(folder.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        const QJsonObject configuration{
            {"name", "Python terminal test"}, {"type", "debugpy"}, {"request", "launch"},
            {"program", source}, {"cwd", folder.path()},
            {"console", "integratedTerminal"}};
        launch.write(QJsonDocument(QJsonObject{{"version", "0.2.0"},
                                         {"configurations", QJsonArray{configuration}}}).toJson());
        launch.close();
        core::SettingsService settings(folder.filePath("settings.ini"));
        workspace::WorkspaceService workspaceService;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspaceService, {search, quick, replace}, false);
        QString error;
        QVERIFY(window.openWorkspaceFolder(folder.path(), &error));
        window.resize(1100, 720);
        window.show();
        auto* panel = window.findChild<ui::RunDebugPanel*>();
        auto* tabs = window.findChild<litecode::ui::components::WorkbenchTabs*>("bottomTabs");
        auto* terminals = window.findChild<QListWidget*>("terminalSessionList");
        auto* terminalPanel = window.findChild<ui::TerminalPanel*>();
        QVERIFY(panel && tabs && terminals && terminalPanel);
        installTestAdapter(*panel, "python", {QDir(sample).filePath("debug_adapter.py")});
        QSignalSpy shellCompleted(terminalPanel, &ui::TerminalPanel::processCompleted);
        panel->toggleBreakpoint(source, 8);
        panel->startDebugging();
        QTRY_VERIFY_WITH_TIMEOUT(panel->isPaused(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(panel->usesIntegratedTerminal(), 30000);
        QCOMPARE(tabs->currentWidget()->objectName(), QString("terminalPanel"));
        bool foundDebugTerminal = false;
        for (int index = 0; index < terminals->count(); ++index)
            foundDebugTerminal |= terminals->item(index)->text().contains("Python Debug Console");
        QVERIFY(foundDebugTerminal);
        panel->continueDebugging();
        QTRY_VERIFY_WITH_TIMEOUT(panel->outputText().contains("Process exited with code 0"),
                                 30000);
        QTest::qWait(200);
        QCOMPARE(shellCompleted.size(), 0);
    }
    void runPanelLoadsVscodeLaunchAndRunsWithoutDebugger() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QFile tasks(workspace.filePath(".vscode/tasks.json"));
        QVERIFY(tasks.open(QIODevice::WriteOnly));
#ifdef Q_OS_WIN
        tasks.write(R"({"version":"2.0.0","tasks":[{"label":"Prepare","type":"process","command":"cmd.exe","args":["/c","echo build-ready"]},{"label":"After","type":"process","command":"cmd.exe","args":["/c","echo post-ready"]}]})");
#else
        tasks.write(R"({"version":"2.0.0","tasks":[{"label":"Prepare","type":"process","command":"/bin/echo","args":["build-ready"]},{"label":"After","type":"process","command":"/bin/echo","args":["post-ready"]}]})");
#endif
        tasks.close();
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
#ifdef Q_OS_WIN
        const QByteArray configuration =
            R"({"version":"0.2.0","configurations":[{"name":"Echo","type":"test","request":"launch","program":"cmd.exe","args":["/c","echo LiteCodeRun"],"preLaunchTask":"Prepare","postDebugTask":"After"}]})";
#else
        const QByteArray configuration =
            R"({"version":"0.2.0","configurations":[{"name":"Echo","type":"test","request":"launch","program":"/bin/echo","args":["LiteCodeRun"],"preLaunchTask":"Prepare","postDebugTask":"After"}]})";
#endif
        QCOMPARE(launch.write(configuration), qint64(configuration.size()));
        launch.close();
        ui::RunDebugPanel panel;
        QCOMPARE(panel.contentLayout()->contentsMargins(), QMargins{});
        panel.setWorkspace(workspace.path());
        auto* picker = panel.findChild<QComboBox*>("debugConfigurationPicker");
        QVERIFY(picker);
        QCOMPARE(picker->count(), 1);
        QCOMPARE(picker->currentText(), QString("Echo"));
        QVERIFY(panel.findChild<QWidget*>("runDebugToolbar"));
        QCOMPARE(panel.findChildren<QToolButton*>("debugSectionHeader").size(), 5);
        QVERIFY(!panel.findChild<QPlainTextEdit*>("debugOutput"));
        panel.runWithoutDebugging();
        QTRY_VERIFY_WITH_TIMEOUT(panel.outputText().contains("LiteCodeRun"), 10000);
        QVERIFY(panel.outputText().contains("build-ready"));
        QTRY_VERIFY_WITH_TIMEOUT(panel.outputText().contains("post-ready"), 10000);
        panel.stop();
    }
    void contributedDebuggerSchemaStopsInvalidLaunch() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(QDir(workspace.path()).mkdir(QStringLiteral(".vscode")));
        QFile launch(workspace.filePath(QStringLiteral(".vscode/launch.json")));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        launch.write(R"({"version":"0.2.0","configurations":[{"name":"Fixture","type":"fixture","request":"launch","interpreter":["python"]}]})");
        launch.close();
        ui::RunDebugPanel panel;
        panel.setDebuggerSchemas({{QStringLiteral("fixture"), QJsonObject{
            {QStringLiteral("launch"), QJsonObject{
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("interpreter"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}}}}}});
        panel.setWorkspace(workspace.path());
        panel.startDebugging();
        auto* notice = panel.findChild<QLabel*>(QStringLiteral("runDebugNotice"));
        QVERIFY(notice);
        QVERIFY2(notice->text().contains(QStringLiteral("interpreter: expected string")),
                 qPrintable(notice->text()));
    }
    void runPanelStartsConfiguredDapAdapter() {
        QTemporaryDir workspace;
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        QJsonObject configuration{{"name", "Fixture"}, {"type", "fixture"}, {"request", "launch"}};
        QJsonObject root{{"version", "0.2.0"},
                         {"configurations", QJsonArray{configuration}}};
        launch.write(QJsonDocument(root).toJson());
        launch.close();
        core::SettingsService settings(workspace.filePath("settings.ini"));
        ui::RunDebugPanel panel(nullptr, &settings);
        panel.setWorkspace(workspace.path());
        const QString source = workspace.filePath("main.cpp");
        panel.toggleBreakpoint(source, 3);
        QVERIFY(panel.hasBreakpoint(source, 3));
        {
            ui::RunDebugPanel reopened(nullptr, &settings);
            reopened.setWorkspace(workspace.path());
            QVERIFY(reopened.hasBreakpoint(source, 3));
        }
        auto* breakpoints = panel.findChild<QTreeWidget*>("debugBreakpoints");
        QVERIFY(breakpoints);
        QCOMPARE(breakpoints->topLevelItemCount(), 1);
        QCOMPARE(breakpoints->topLevelItem(0)->text(0), QString("main.cpp"));
        QCOMPARE(breakpoints->topLevelItem(0)->text(2), QString("3"));
        installTestAdapter(panel, LITECODE_DAP_ADAPTER_FIXTURE, {"--session"});
        panel.startDebugging();
        auto* notice = panel.findChild<QLabel*>();
        QVERIFY(notice);
        QTRY_VERIFY_WITH_TIMEOUT(notice->text().contains("Debug state: 4"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(panel.outputText().contains("hello world\n"), 10000);
        QVERIFY(!panel.outputText().contains("debugpy"));
        QSignalSpy locationSpy(&panel, &ui::RunDebugPanel::pausedLocationChanged);
        panel.pauseDebugging();
        auto* frames = panel.findChild<QTreeWidget*>("debugCallStack");
        auto* variables = panel.findChild<QTreeWidget*>("debugVariables");
        QVERIFY(frames && variables);
        QTRY_COMPARE_WITH_TIMEOUT(frames->topLevelItemCount(), 1, 10000);
        QCOMPARE(frames->topLevelItem(0)->text(0), QString("fixtureFrame"));
        QCOMPARE(frames->topLevelItem(0)->text(1), QString("fixture.cpp"));
        QCOMPARE(frames->topLevelItem(0)->text(2), QString("7"));
        QTRY_VERIFY_WITH_TIMEOUT(!locationSpy.isEmpty(), 10000);
        QCOMPARE(locationSpy.last().at(0).toString(), QString("fixture.cpp"));
        QCOMPARE(locationSpy.last().at(1).toInt(), 7);
        QTRY_COMPARE_WITH_TIMEOUT(variables->topLevelItemCount(), 1, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(variables->topLevelItem(0)->childCount(), 1, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(variables->topLevelItem(0)->child(0)->text(0),
                                  QString("answer = 42"), 10000);
        panel.evaluateConsole(QStringLiteral("answer"));
        QTRY_VERIFY_WITH_TIMEOUT(panel.outputText().contains(QStringLiteral("> answer\n120\n")),
                                 10000);
        panel.toggleBreakpoint(source, 3);
        QVERIFY(!panel.hasBreakpoint(source, 3));
        QCOMPARE(breakpoints->topLevelItemCount(), 0);
        panel.stop();
    }
    void runPanelResolvesAdapterFromExtension() {
        QTemporaryDir workspace;
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        const QJsonObject configuration{{"name", "Fixture"}, {"type", "fixture"},
                                        {"request", "launch"}};
        const QJsonObject root{{"version", "0.2.0"},
                               {"configurations", QJsonArray{configuration}}};
        launch.write(QJsonDocument(root).toJson());
        launch.close();
        ui::RunDebugPanel panel;
        panel.setWorkspace(workspace.path());
        QSignalSpy requested(&panel, &ui::RunDebugPanel::debugAdapterRequested);
        panel.startDebugging();
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.at(0).at(1).toString(), QString("fixture"));
        const auto firstId = requested.at(0).at(0).toULongLong();
        panel.stop();
        const QJsonObject descriptor{{"type", "executable"}, {"command", LITECODE_DAP_ADAPTER_FIXTURE},
            {"args", QJsonArray{"--session"}}};
        panel.completeDebugAdapterRequest(firstId, descriptor, {});
        panel.startDebugging();
        QCOMPARE(requested.size(), 2);
        const auto secondId = requested.at(1).at(0).toULongLong();
        QVERIFY(secondId != firstId);
        panel.completeDebugAdapterRequest(secondId, descriptor, {});
        auto* notice = panel.findChild<QLabel*>("runDebugNotice");
        QVERIFY(notice);
        QTRY_VERIFY_WITH_TIMEOUT(notice->text().contains("Debug state: 4"), 10000);
        panel.stop();
    }
    void runDebugUsesSharedSidebarTitle() {
        ui::Theme::apply(*qApp, true);
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        launch.write(R"({"version":"0.2.0","configurations":[{"name":"Demo","type":"test","request":"launch","program":"demo"}]})");
        launch.close();
        core::SettingsService settings(workspace.filePath("settings.ini"));
        workspace::WorkspaceService workspaceService;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspaceService, {search, quick, replace}, false);
        QString error;
        QVERIFY(window.openWorkspaceFolder(workspace.path(), &error));
        auto* controller = window.findChild<ui::WorkbenchController*>();
        QVERIFY(controller);
        controller->showSidebar(ui::SidebarView::RunDebug);
        auto* panel = window.findChild<ui::RunDebugPanel*>();
        auto* dock = window.findChild<QDockWidget*>("workspaceDock");
        auto* actions = window.findChild<QWidget*>("runDebugToolbar");
        QVERIFY(panel && dock && actions);
        QVERIFY(dock->titleBarWidget()->isAncestorOf(actions));
        QCOMPARE(panel->contentLayout()->indexOf(actions), -1);
        QVERIFY(!actions->isHidden());
        QVERIFY(panel->findChild<QWidget*>("debugWelcome")->isHidden());
        QVERIFY(panel->findChild<QTreeWidget*>("debugVariables")->isVisibleTo(panel));
        auto* bottomTabs = window.findChild<litecode::ui::components::WorkbenchTabs*>("bottomTabs");
        QVERIFY(bottomTabs);
        QVERIFY(!bottomTabs->tabBar()->isHidden());
        QCOMPARE(bottomTabs->tabText(0), QString("Terminal"));
        auto* console = window.findChild<QWidget*>("debugConsolePage");
        QVERIFY(console && bottomTabs->indexOf(console) >= 0);
        QCOMPARE(bottomTabs->tabText(bottomTabs->indexOf(console)), QString("Debug Console"));
        const QString capturePath = qEnvironmentVariable("LITECODE_RUN_DEBUG_CAPTURE");
        if (!capturePath.isEmpty()) {
            window.resize(1100, 720);
            window.show();
            QTest::qWait(200);
            QVERIFY(window.grab().save(capturePath));
        }
        controller->showSidebar(ui::SidebarView::Explorer);
        QVERIFY(actions->isHidden());
        controller->showSidebar(ui::SidebarView::RunDebug);
        QVERIFY(!actions->isHidden());
    }
    void pausedDebugSessionSharesBottomPanelWithTerminal() {
        ui::Theme::apply(*qApp, false);
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        const QString source = workspace.filePath("sample.cpp");
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        sourceFile.write("int main() {\n  int a = 1;\n  int b = 2;\n  int c = a + b;\n"
                         "  c += 4;\n  c -= 2;\n  return c;\n}\n");
        sourceFile.close();
        QFile launch(workspace.filePath(".vscode/launch.json"));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        const QJsonObject configuration{{"name", "Fixture"}, {"type", "fixture"},
                                        {"request", "launch"}, {"program", source}};
        launch.write(QJsonDocument(QJsonObject{{"version", "0.2.0"},
                                               {"configurations", QJsonArray{configuration}}})
                         .toJson());
        launch.close();
        core::SettingsService settings(workspace.filePath("settings.ini"));
        settings.saveTheme(QStringLiteral("light"));
        workspace::WorkspaceService workspaceService;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspaceService, {search, quick, replace}, false);
        QString error;
        QVERIFY(window.openWorkspaceFolder(workspace.path(), &error));
        auto* controller = window.findChild<ui::WorkbenchController*>();
        auto* panel = window.findChild<ui::RunDebugPanel*>();
        auto* editorArea = window.findChild<ui::EditorArea*>();
        auto* bottomTabs = window.findChild<litecode::ui::components::WorkbenchTabs*>("bottomTabs");
        QVERIFY(controller && panel && editorArea && bottomTabs);
        installTestAdapter(*panel, LITECODE_DAP_ADAPTER_FIXTURE, {"--session"});
        controller->showSidebar(ui::SidebarView::RunDebug);
        QVERIFY(editorArea->openFile(source, &error));
        panel->toggleBreakpoint(source, 7);
        panel->startDebugging();
        auto* notice = panel->findChild<QLabel*>();
        QVERIFY(notice);
        QTRY_VERIFY_WITH_TIMEOUT(notice->text().contains("Debug state: 4"), 10000);
        panel->pauseDebugging();
        auto* frames = panel->findChild<QTreeWidget*>("debugCallStack");
        QVERIFY(frames);
        QTRY_COMPARE_WITH_TIMEOUT(frames->topLevelItemCount(), 1, 10000);
        QCOMPARE(frames->topLevelItem(0)->text(1), QString("sample.cpp"));
        QCOMPARE(frames->topLevelItem(0)->text(2), QString("7"));
        QCOMPARE(editorArea->currentFile(), source);
        QCOMPARE(bottomTabs->currentWidget()->objectName(), QString("debugConsolePage"));
        QVERIFY(!bottomTabs->tabBar()->isHidden());
        window.resize(1100, 720);
        window.show();
        QTest::qWait(50);
        // Initial window restoration can select the saved panel tab. Exercise the
        // visible workbench exactly as a user would after the window has opened.
        controller->showBottomPanel(ui::BottomPanelView::DebugConsole);
        auto* terminalToolbar = window.findChild<QWidget*>("terminalToolbar");
        QVERIFY(terminalToolbar);
        QVERIFY(!terminalToolbar->isVisible());
        controller->showBottomPanel(ui::BottomPanelView::Terminal);
        QCOMPARE(bottomTabs->tabText(bottomTabs->currentIndex()), QString("Terminal"));
        QVERIFY(terminalToolbar->isVisible());
        controller->showBottomPanel(ui::BottomPanelView::DebugConsole);
        QCOMPARE(bottomTabs->currentWidget()->objectName(), QString("debugConsolePage"));
        QVERIFY(!terminalToolbar->isVisible());
        const QString capturePath = qEnvironmentVariable("LITECODE_PAUSED_DEBUG_CAPTURE");
        if (!capturePath.isEmpty()) {
            QTest::qWait(200);
            QVERIFY(window.grab().save(capturePath));
        }
        panel->stop();
    }
    void externalFileGutterClickPlacesBreakpoint() {
        ui::Theme::apply(*qApp, false);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir(QStringLiteral("workspace")));
        const QString workspacePath = fixture.filePath(QStringLiteral("workspace"));
        const QString source = qEnvironmentVariable("LITECODE_GUTTER_SOURCE").isEmpty()
            ? fixture.filePath(QStringLiteral("example.py"))
            : qEnvironmentVariable("LITECODE_GUTTER_SOURCE");
        if (qEnvironmentVariable("LITECODE_GUTTER_SOURCE").isEmpty()) {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("\n\n\ndef calculate_total():\n    subtotal = 0\n    return subtotal\n");
            file.close();
        }
        core::SettingsService settings(fixture.filePath(QStringLiteral("settings.ini")));
        settings.saveTheme(QStringLiteral("light"));
        workspace::WorkspaceService workspaceService;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspaceService, {search, quick, replace}, false);
        QString error;
        QVERIFY(window.openWorkspaceFolder(workspacePath, &error));
        auto* editorArea = window.findChild<ui::EditorArea*>();
        auto* panel = window.findChild<ui::RunDebugPanel*>();
        QVERIFY(editorArea && panel);
        // This gutter test supplies the language declaration independently of
        // the separate native-extension integration tests.
        panel->setBreakpointLanguages({"python"});
        editorArea->setBreakpointLanguages({"python"});
        QVERIFY(editorArea->openFile(source, &error));
        window.resize(1100, 720);
        window.show();
        window.raise();
        window.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 5000);
        ScintillaEditBase* control = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((control = editorArea->findChild<ScintillaEditBase*>(
                                      QStringLiteral("scintillaControl"))), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(control->isVisible(), 5000);
        auto* editor = editorArea->findChild<editor::ScintillaEditor*>();
        QVERIFY(editor);
        QVERIFY(!editor->isReadOnly());
        const int lineHeight = editor->diffLineHeight();
        const int glyphWidth = static_cast<int>(control->send(
            static_cast<unsigned int>(Scintilla::Message::GetMarginWidthN), 0));
        const int numberWidth = static_cast<int>(control->send(
            static_cast<unsigned int>(Scintilla::Message::GetMarginWidthN), 1));
        const int decorationsWidth = static_cast<int>(control->send(
            static_cast<unsigned int>(Scintilla::Message::GetMarginWidthN), 2));
        QCOMPARE(decorationsWidth, 26);
        const QPoint gutterPoint(8, lineHeight * 4 + lineHeight / 2);
        QCursor::setPos(control->mapToGlobal(QPoint(160, gutterPoint.y())));
        QTest::qWait(100);
        QCursor::setPos(control->mapToGlobal(gutterPoint));
        QTRY_VERIFY(control->send(
            static_cast<unsigned int>(Scintilla::Message::MarkerGet), 4) & (1 << 19));
        const QString capturePath = qEnvironmentVariable("LITECODE_GUTTER_WINDOW_CAPTURE");
        if (!capturePath.isEmpty())
            QVERIFY(window.grab().save(capturePath + QStringLiteral(".hover.png")));
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows")) {
            // Exercise Windows' actual pointer dispatch, not QTest's direct
            // widget delivery: Scintilla routes native clicks to its viewport.
            INPUT press{};
            press.type = INPUT_MOUSE;
            press.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            INPUT release = press;
            release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            QCOMPARE(QApplication::widgetAt(control->mapToGlobal(gutterPoint)),
                     control->viewport());
            QCOMPARE(SendInput(1, &press, sizeof(INPUT)), UINT(1));
            QCOMPARE(SendInput(1, &release, sizeof(INPUT)), UINT(1));
        } else {
            QTest::mouseClick(control->viewport(), Qt::LeftButton, Qt::NoModifier,
                              control->viewport()->mapFrom(control, gutterPoint));
        }
#else
        QTest::mouseClick(control->viewport(), Qt::LeftButton, Qt::NoModifier,
                          control->viewport()->mapFrom(control, gutterPoint));
#endif
        QTRY_VERIFY(panel->hasBreakpoint(source, 5));
        QTRY_VERIFY(control->send(
            static_cast<unsigned int>(Scintilla::Message::MarkerGet), 4) & (1 << 22));
        if (!capturePath.isEmpty())
            QVERIFY(window.grab().save(capturePath + QStringLiteral(".set.png")));
        QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier,
                          QPoint(glyphWidth + numberWidth / 2, gutterPoint.y()));
        QTRY_VERIFY(!panel->hasBreakpoint(source, 5));
        QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier,
                          QPoint(glyphWidth + numberWidth + decorationsWidth + 8,
                                 gutterPoint.y()));
        QTRY_VERIFY(panel->hasBreakpoint(source, 5));
    }
    void openingRepositoryFolderUsesTheSharedWorkspacePath() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString checkout = directory.filePath("linked-worktree");
        QVERIFY(QDir().mkdir(checkout));
        core::SettingsService settings(directory.filePath("settings.ini"));
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
        QString diagnostic;
        QVERIFY(window.openWorkspaceFolder(checkout, &diagnostic));
        QCOMPARE(QDir::fromNativeSeparators(workspace.rootPath()),
                 QDir(checkout).canonicalPath());
        QVERIFY(!window.openWorkspaceFolder(directory.filePath("missing"), &diagnostic));
        QVERIFY(!diagnostic.isEmpty());
        QCOMPARE(QDir::fromNativeSeparators(workspace.rootPath()),
                 QDir(checkout).canonicalPath());
    }
    void changesAndGraphUseTheSameSidebarWrapper() {
        for (bool dark : {false, true}) {
            ui::Theme::apply(*qApp, dark);
            ui::SourceControlPanel panel;
            panel.resize(360, 500);
            panel.show();
            auto* pages = panel.findChild<QStackedWidget*>("scmViews");
            QVERIFY(pages);
            auto* changes = qobject_cast<ui::components::SidebarView*>(pages->widget(0));
            auto* graph = qobject_cast<ui::components::SidebarView*>(pages->widget(1));
            QVERIFY(changes && graph);
            QCOMPARE(changes->contentLayout()->contentsMargins(), QMargins{});
            QCOMPARE(graph->contentLayout()->contentsMargins(),
                     changes->contentLayout()->contentsMargins());
            QCOMPARE(graph->contentLayout()->spacing(), changes->contentLayout()->spacing());
            auto* emptyWelcome = panel.findChild<QWidget*>("scmEmptyWelcome");
            QVERIFY(emptyWelcome);
            QCOMPARE(emptyWelcome->layout()->contentsMargins(), QMargins(20, 14, 20, 0));
            QCOMPARE(panel.findChild<QLabel*>("scmNotice")->contentsMargins(),
                     panel.findChild<QWidget*>("scmInputArea")->layout()->contentsMargins());
            auto* toolbar = panel.findChild<QWidget*>("sourceControlToolbar");
            const auto margins = toolbar->layout()->contentsMargins();
            QCOMPARE(margins, ui::components::SidebarView::toolbarMargins());
            const auto changesRect = changes->geometry();
            const auto toolbarRect = toolbar->geometry();
            panel.findChild<QAction*>("scmHistoryView")->trigger();
            QTRY_COMPARE(graph->geometry(), changesRect);
            QCOMPARE(toolbar->geometry(), toolbarRect);
            QCOMPARE(toolbar->layout()->contentsMargins(), margins);
            panel.findChild<QAction*>("scmChangesView")->trigger();
            QTRY_COMPARE(changes->geometry(), changesRect);
            QCOMPARE(toolbar->layout()->contentsMargins(), margins);
        }
    }
    void nonRepositoryWelcomeUsesInsetLayout() {
        ui::Theme::apply(*qApp, false);
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        ui::SourceControlPanel panel;
        panel.resize(420, 500);
        panel.setWorkspace(folder.path());
        panel.show();
        auto* toolbar = panel.findChild<QWidget*>("sourceControlToolbar");
        auto* welcome = panel.findChild<QWidget*>("scmEmptyWelcome");
        auto* message = panel.findChild<QLabel*>("scmChangesEmpty");
        auto* button = panel.findChild<QPushButton*>("gitInitializeRepository");
        QVERIFY(toolbar && welcome && message && button);
        QTRY_VERIFY_WITH_TIMEOUT(welcome->isVisible(), 5000);
        QVERIFY(!toolbar->isVisible());
        QVERIFY(!panel.findChild<QTreeWidget*>("gitChanges")->isVisible());
        QVERIFY(message->text().contains("doesn't have a Git repository"));
        QCOMPARE(welcome->layout()->contentsMargins(), QMargins(20, 14, 20, 0));
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(), 5000);
        QCOMPARE(button->width(), welcome->width() - 40);
        const QString capture = qEnvironmentVariable("LITECODE_NO_REPOSITORY_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(panel.grab().save(capture));
    }
    void gitOverflowUsesGroupedActionsInBothViews() {
        QTemporaryDir repository;
        QProcess git;
        git.setWorkingDirectory(repository.path());
        git.start("git", {"init", "--initial-branch=main"});
        QVERIFY(git.waitForFinished(10000));
        QCOMPARE(git.exitCode(), 0);
        ui::SourceControlPanel panel;
        panel.resize(430, 600);
        panel.show();
        panel.setWorkspace(repository.path());
        auto* service =
            panel.findChild<workspace::GitService*>(QString{}, Qt::FindDirectChildrenOnly);
        QTRY_VERIFY(!service->busy() && !service->repository().isEmpty());
        auto* more = panel.findChild<QToolButton*>("scmMore");
        QVERIFY(more);
        QCOMPARE(panel.findChildren<QToolButton*>("scmMore").size(), 1);
        QSignalSpy outputRequested(&panel, &ui::SourceControlPanel::gitOutputRequested);
        for (bool graph : {false, true}) {
            panel.findChild<QAction*>(graph ? "scmHistoryView" : "scmChangesView")->trigger();
            QStringList topLevel;
            bool nestedChanges = false, nestedBranch = false, nestedRecovery = false;
            bool nestedStash = false;
            bool nestedTags = false;
            bool nestedWorktrees = false;
            bool nestedPullPush = false;
            bool unchangedViewChoices = false;
            bool noVerifyHidden = false;
            QTimer::singleShot(30, &panel, [&] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                for (auto* action : menu->actions()) {
                    if (action->isSeparator())
                        continue;
                    topLevel.append(action->text());
                    if (!action->menu())
                        continue;
                    QStringList children;
                    for (auto* child : action->menu()->actions())
                        if (!child->isSeparator())
                            children.append(child->text());
                    if (action->text() == "Changes")
                        nestedChanges =
                            children == QStringList{"Stage All Changes", "Unstage All Changes",
                                                    "Discard All Changes"};
                    if (action->text() == "Pull, Push")
                        nestedPullPush =
                            children == QStringList{"Sync", "Sync (Rebase)", "Pull",
                                                    "Pull (Rebase)", "Pull From...", "Push",
                                                    "Push To...", "Fetch",
                                                    "Fetch (Prune)", "Fetch All Remotes"};
                    if (action->text() == "Branch")
                        nestedBranch = children.size() == 8 && children[0].startsWith("Merge") &&
                                       children[1].startsWith("Rebase Branch") &&
                                       children[2].startsWith("Create Branch") &&
                                       children[3].startsWith("Create Branch From") &&
                                       children[4].startsWith("Rename Branch") &&
                                       children[5].startsWith("Delete Branch") &&
                                       children[6].startsWith("Delete Remote Branch") &&
                                       children[7].startsWith("Publish Branch");
                    if (action->text() == "Stash")
                        nestedStash = children.size() == 10 && children[0] == "Stash" &&
                                      children[1].startsWith("Stash (Include") &&
                                      children[2] == "Stash Staged" &&
                                      children[9].startsWith("View Stash");
                    if (action->text() == "Recovery")
                        nestedRecovery = children.size() == 3 &&
                                         children[0].startsWith("Restore Checkout") &&
                                         children[1].startsWith("Replace Conflicting Tag") &&
                                         children[2].startsWith("Restore Tag Backup");
                    if (action->text() == "Tags")
                        nestedTags = children.size() == 4 &&
                                     children[0].startsWith("Create Tag") &&
                                     children[1].startsWith("Delete Tag") &&
                                     children[2].startsWith("Delete Remote Tag") &&
                                     children[3] == "Push Tags";
                    if (action->text() == "Worktrees")
                        nestedWorktrees = children == QStringList{"Create Worktree..."};
                    if (action->text() == "Views")
                        unchangedViewChoices = children == QStringList{"Changes", "Graph"};
                    if (action->text() == "Commit")
                        noVerifyHidden = std::none_of(children.cbegin(), children.cend(),
                                                      [](const QString& label) {
                                                          return label.contains("No Verify");
                                                      });
                }
                for (auto* action : menu->actions())
                    if (action->text() == "Show Git Output")
                        action->trigger();
                menu->close();
            });
            more->click();
            QVERIFY(nestedChanges && nestedBranch && nestedRecovery && nestedTags &&
                    nestedWorktrees && nestedStash &&
                    nestedPullPush &&
                    unchangedViewChoices && noVerifyHidden);
            QVERIFY(!topLevel.contains("Stage All Changes"));
            QVERIFY(!topLevel.contains("Unstage All Changes"));
            QVERIFY(topLevel.contains("Clone..."));
            QVERIFY(topLevel.contains("Show Git Output"));
            for (const auto& group : {"Commit", "Changes", "Pull, Push", "Branch", "Remote",
                                      "Stash", "Tags", "Worktrees", "Recovery", "Git Settings"})
                QCOMPARE(topLevel.count(group), 1);
        }
        QCOMPARE(outputRequested.size(), 2);
        auto forcePreferences = panel.gitPreferences();
        forcePreferences.allowForcePush = true;
        panel.setGitPreferences(forcePreferences);
        bool forceItemsVisible = false;
        QTimer::singleShot(30, &panel, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
                return;
            for (auto* action : menu->actions()) {
                if (action->text() != "Pull, Push" || !action->menu())
                    continue;
                QStringList labels;
                for (auto* item : action->menu()->actions())
                    if (!item->isSeparator())
                        labels.append(item->text());
                forceItemsVisible = labels.contains("Push (Force)") &&
                                    labels.contains("Push to... (Force)");
            }
            menu->close();
        });
        more->click();
        QVERIFY(forceItemsVisible);
        auto commitPreferences = panel.gitPreferences();
        commitPreferences.allowNoVerifyCommit = true;
        panel.setGitPreferences(commitPreferences);
        bool noVerifyVisible = false;
        QTimer::singleShot(30, &panel, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
                return;
            for (auto* action : menu->actions()) {
                if (action->text() != "Commit" || !action->menu())
                    continue;
                QStringList labels;
                for (auto* item : action->menu()->actions())
                    if (!item->isSeparator())
                        labels.append(item->text());
                noVerifyVisible = labels.contains("Commit (No Verify)") &&
                                  labels.contains("Commit Staged (No Verify)") &&
                                  labels.contains("Commit All (No Verify)");
            }
            menu->close();
        });
        more->click();
        QVERIFY(noVerifyVisible);
    }
    void linkedWorktreeGetsContextSpecificMenu() {
        QTemporaryDir repository, parent;
        QProcess git;
        git.setWorkingDirectory(repository.path());
        git.start("git", {"init", "--initial-branch=main"});
        QVERIFY(git.waitForFinished(10000));
        QCOMPARE(git.exitCode(), 0);
        QFile file(repository.filePath("base.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("base\n"), qint64(5));
        file.close();
        for (const QStringList arguments : {
                 QStringList{"-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                             "add", "base.txt"},
                 QStringList{"-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                             "commit", "-m", "base"},
                 QStringList{"worktree", "add", "-b", "linked", parent.filePath("linked")}}) {
            git.start("git", arguments);
            QVERIFY(git.waitForFinished(10000));
            QCOMPARE(git.exitCode(), 0);
        }
        ui::SourceControlPanel panel;
        panel.resize(430, 600);
        panel.show();
        panel.setWorkspace(parent.filePath("linked"));
        auto* service = panel.findChild<workspace::GitService*>(
            QString{}, Qt::FindDirectChildrenOnly);
        QTRY_VERIFY(service && !service->busy() && !service->repository().isEmpty());
        QStringList worktreeActions;
        QTimer::singleShot(30, &panel, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
                return;
            for (auto* action : menu->actions()) {
                if (action->text() != "Worktrees" || !action->menu())
                    continue;
                for (auto* child : action->menu()->actions())
                    if (!child->isSeparator())
                        worktreeActions.append(child->text());
            }
            menu->close();
        });
        panel.findChild<QToolButton*>("scmMore")->click();
        QCOMPARE(worktreeActions,
                 (QStringList{"Open Worktree", "Open Worktree in New Window",
                              "Delete Worktree..."}));
    }
    void divergenceRowsKeepRevisionPairsDistinct() {
        ui::GitHistoryWidget graph;
        graph.resize(400, 300);
        graph.show();
        auto* service = graph.findChild<workspace::GitService*>();
        auto* tree = graph.findChild<QTreeWidget*>();
        QVERIFY(service && tree);
        const auto head = QString(40, 'a'), base = QString(40, 'b'), upstream = QString(40, 'c');
        workspace::GitCommit commit;
        commit.id = head;
        commit.parents = {base};
        commit.subject = "Local commit";
        commit.references = "HEAD -> refs/heads/main";
        service->historyReady({commit});
        workspace::GitDivergence divergence;
        divergence.head = head;
        divergence.upstream = upstream;
        divergence.mergeBase = base;
        divergence.incoming = divergence.outgoing = 1;
        service->divergenceReady(divergence);
        QCOMPARE(tree->topLevelItemCount(), 3);
        QVERIFY(tree->topLevelItem(0)->text(0).startsWith("Incoming Changes"));
        QVERIFY(tree->topLevelItem(1)->text(0).startsWith("Outgoing Changes"));
        service->revisionFilesReady(head, base, {{"local.cpp", {}, 'A'}});
        // The aggregate row and commit share this pair; both must be populated.
        QCOMPARE(tree->topLevelItem(1)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(2)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 0);
        service->revisionFilesReady(upstream, base, {{"remote.cpp", {}, 'M'}});
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        service->revisionFilesReady(head, QString(40, 'd'), {{"wrong.cpp", {}, 'A'}});
        QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QString("local.cpp"));
        graph.revealCurrentCommit();
        QCOMPARE(tree->currentItem(), tree->topLevelItem(2));
        graph.navigateCommit(-1);
        QCOMPARE(tree->currentItem(), tree->topLevelItem(1));
    }
    void installedGitUsesNativeAskpassWithoutTerminal() {
        QTemporaryDir repository;
        QProcess git;
        git.setWorkingDirectory(repository.path());
        const auto run = [&](const QStringList& arguments) {
            git.start("git", arguments);
            return git.waitForFinished(10000) && git.exitCode() == 0;
        };
        QVERIFY(run({"init", "--initial-branch=main"}));
        // Disable external credential managers only in this temporary fixture.
        QVERIFY(run({"config", "credential.helper", ""}));
        QTcpServer http;
        QVERIFY(http.listen(QHostAddress::LocalHost));
        connect(&http, &QTcpServer::newConnection, &http, [&] {
            while (auto* socket = http.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    socket->readAll();
                    socket->write(
                        "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic "
                        "realm=LiteCodeTest\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        QVERIFY(run({"remote", "add", "origin",
                     QString("http://127.0.0.1:%1/test.git").arg(http.serverPort())}));
        QWidget owner;
        owner.resize(800, 600);
        owner.show();
        ui::GitCredentialBroker broker(&owner);
        workspace::GitService service;
        service.setWorkspace(repository.path());
        QTRY_VERIFY(!service.busy() && !service.repository().isEmpty());
        QTimer driver;
        int usernamePrompts = 0, passwordPrompts = 0;
        connect(&driver, &QTimer::timeout, &owner, [&] {
            auto* dialog =
                dynamic_cast<ui::components::QuickInputDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "gitCredentialDialog")
                return;
            const bool password = dialog->input()->echoMode() == QLineEdit::Password;
            password ? ++passwordPrompts : ++usernamePrompts;
            dialog->input()->setText(password ? "dummy-secret-not-an-account" : "dummy-user");
            dialog->accept();
        });
        driver.start(20);
        QSignalSpy completed(&service, &workspace::GitService::operationFinished),
            errors(&service, &workspace::GitService::error);
        service.fetch();
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15000);
        QTRY_VERIFY(!service.busy());
        QVERIFY(usernamePrompts > 0);
        QVERIFY(passwordPrompts > 0);
        QVERIFY(!errors.isEmpty());
        for (const auto& error : errors)
            QVERIFY(!error[0].toString().contains("dummy-secret-not-an-account"));
    }
    void repositorySwitchKeepsIndependentCommitMessages() {
        QTemporaryDir first, second;
        for (const auto& path : {first.path(), second.path()}) {
            QProcess git;
            git.setWorkingDirectory(path);
            git.start("git", {"init", "--initial-branch=main"});
            QVERIFY(git.waitForFinished(10000));
            QCOMPARE(git.exitCode(), 0);
        }
        ui::SourceControlPanel panel;
        panel.resize(380, 600);
        panel.show();
        auto* service =
            panel.findChild<workspace::GitService*>(QString{}, Qt::FindDirectChildrenOnly);
        auto* message = panel.findChild<QPlainTextEdit*>("gitCommitMessage");
        QVERIFY(service && message);
        panel.setWorkspace(first.path());
        QTRY_VERIFY(!service->busy() && !service->repository().isEmpty());
        const auto firstRoot = service->repository();
        message->setPlainText("First repository message");
        panel.selectRepository(second.path());
        QTRY_VERIFY(!service->busy() && !service->repository().isEmpty());
        QVERIFY(service->repository() != firstRoot);
        QVERIFY(message->toPlainText().isEmpty());
        message->setPlainText("Second repository message");
        panel.selectRepository(first.path());
        QTRY_VERIFY(!service->busy() && service->repository() == firstRoot);
        QCOMPARE(message->toPlainText(), QString("First repository message"));
        panel.selectRepository(second.path());
        QTRY_VERIFY(!service->busy() && !service->repository().isEmpty());
        QCOMPARE(message->toPlainText(), QString("Second repository message"));
    }
    void authenticationBridgeMasksCancelsAndRejectsStaleRequests() {
        QWidget owner;
        owner.resize(800, 600);
        owner.show();
        ui::GitCredentialBroker broker(&owner);
        const auto operation = QUuid::createUuid().toString(QUuid::Id128);
        const auto key = ("litecode.git.active." + operation).toUtf8();
        qApp->setProperty(key, true);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LITECODE_GIT_ASKPASS_ENDPOINT",
                           qApp->property("litecode.git.askpass.endpoint").toString());
        environment.insert("LITECODE_GIT_ASKPASS_TOKEN",
                           qApp->property("litecode.git.askpass.token").toString());
        environment.insert("LITECODE_GIT_ASKPASS_OPERATION", operation);
        QProcess helper;
        helper.setProcessEnvironment(environment);
        QTimer driver;
        bool masked = false;
        int mode = 0;
        connect(&driver, &QTimer::timeout, &owner, [&] {
            if (mode >= 3) {
                auto* trust =
                    dynamic_cast<ui::ConfirmationDialog*>(QApplication::activeModalWidget());
                if (trust) {
                    auto* button = trust->findChild<QPushButton*>(
                        mode == 3 ? "confirmationCancel" : "confirmationPrimary");
                    if (button)
                        button->click();
                }
                return;
            }
            auto* dialog =
                dynamic_cast<ui::components::QuickInputDialog*>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "gitCredentialDialog")
                return;
            masked = dialog->input()->echoMode() == QLineEdit::Password;
            if (mode == 0) {
                dialog->input()->setText(" dummy test credential ");
                dialog->accept();
            } else if (mode == 1) {
                dialog->reject();
            } else {
                qApp->setProperty(key, QVariant{});
            }
        });
        driver.start(20);
        helper.start(QString::fromUtf8(LITECODE_GIT_ASKPASS_HELPER),
                     {"Password for 'https://example.invalid':"});
        QTRY_COMPARE_WITH_TIMEOUT(helper.state(), QProcess::NotRunning, 10000);
        QVERIFY(masked);
        QCOMPARE(helper.exitCode(), 0);
        QCOMPARE(helper.readAllStandardOutput(), QByteArray(" dummy test credential \n"));
        QVERIFY(helper.readAllStandardError().isEmpty());
        mode = 1;
        helper.start(QString::fromUtf8(LITECODE_GIT_ASKPASS_HELPER),
                     {"Enter passphrase for key 'test':"});
        QTRY_COMPARE_WITH_TIMEOUT(helper.state(), QProcess::NotRunning, 10000);
        QVERIFY(masked);
        QVERIFY(helper.exitCode() != 0);
        QVERIFY(helper.readAllStandardOutput().isEmpty());
        mode = 2;
        helper.start(QString::fromUtf8(LITECODE_GIT_ASKPASS_HELPER), {"Password:"});
        QTRY_COMPARE_WITH_TIMEOUT(helper.state(), QProcess::NotRunning, 10000);
        QVERIFY(helper.exitCode() != 0);
        QVERIFY(helper.readAllStandardOutput().isEmpty());
        qApp->setProperty(key, true);
        mode = 3;
        helper.start(QString::fromUtf8(LITECODE_GIT_ASKPASS_HELPER),
                     {"Host test fingerprint SHA256:dummy. Are you sure you want to continue "
                      "connecting (yes/no)?"});
        QTRY_COMPARE_WITH_TIMEOUT(helper.state(), QProcess::NotRunning, 10000);
        QVERIFY(helper.exitCode() != 0);
        QVERIFY(helper.readAllStandardOutput().isEmpty());
        mode = 4;
        helper.start(QString::fromUtf8(LITECODE_GIT_ASKPASS_HELPER),
                     {"Host test fingerprint SHA256:dummy. Are you sure you want to continue "
                      "connecting (yes/no)?"});
        QTRY_COMPARE_WITH_TIMEOUT(helper.state(), QProcess::NotRunning, 10000);
        QCOMPARE(helper.exitCode(), 0);
        QCOMPARE(helper.readAllStandardOutput(), QByteArray("yes\n"));
        driver.stop();
        qApp->setProperty(key, true);
        environment.insert("LITECODE_GIT_ASKPASS_TOKEN", "invalid-token");
        helper.setProcessEnvironment(environment);
        helper.start(QString::fromUtf8(LITECODE_GIT_ASKPASS_HELPER), {"Password:"});
        QTRY_COMPARE_WITH_TIMEOUT(helper.state(), QProcess::NotRunning, 10000);
        QVERIFY(helper.exitCode() != 0);
        QVERIFY(helper.readAllStandardOutput().isEmpty());
        qApp->setProperty(key, QVariant{});
    }
    void groupedReferenceBadges() {
        const auto badges = ui::gitReferenceBadges(
            "HEAD -> refs/heads/main, refs/heads/feature/name, refs/remotes/origin/main, tag: "
            "refs/tags/v1, tag: refs/tags/v2, refs/litecode/recovery/tags/ignored");
        QCOMPARE(badges.size(), 4);
        QCOMPARE(badges[0].label, QString("main"));
        QCOMPARE(badges[1].count, 1);
        QCOMPARE(badges[2].count, 1);
        QCOMPARE(badges[3].count, 2);
    }
    void branchPopupValidatesBeforeAccepting() {
        QTemporaryDir repository;
        QProcess git;
        git.setWorkingDirectory(repository.path());
        auto run = [&](const QStringList& arguments) {
            git.start("git", arguments);
            return git.waitForFinished(10000) && git.exitCode() == 0;
        };
        QVERIFY(run({"init", "--initial-branch=main"}));
        QVERIFY(run({"config", "user.name", "UI Test"}));
        QVERIFY(run({"config", "user.email", "ui@example.invalid"}));
        QFile file(repository.filePath("file.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("base\n");
        file.close();
        QVERIFY(run({"add", "."}));
        QVERIFY(run({"-c", "commit.gpgsign=false", "commit", "-m", "base"}));
        QWidget owner;
        owner.resize(800, 600);
        owner.show();
        QTimer driver;
        int phase = 0;
        bool duplicateBlocked = false;
        connect(&driver, &QTimer::timeout, &owner, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto* input = dialog->findChild<QLineEdit*>("gitInput");
            auto* validation = dialog->findChild<QLabel*>("quickInputValidation");
            if (!input || !validation)
                return;
            if (phase == 0) {
                input->setText("main");
                ++phase;
            } else if (phase == 1 && validation->text().contains("already exists")) {
                QTest::keyClick(input, Qt::Key_Return);
                duplicateBlocked = dialog->isVisible();
                input->setText("feature new");
                ++phase;
            } else if (phase == 2 && validation->text().isEmpty() &&
                       !dialog->property("validationPending").toBool()) {
                QTest::keyClick(input, Qt::Key_Return);
                ++phase;
            }
        });
        bool timedOut = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &owner, [&] {
            timedOut = true;
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        timeout.start(10000);
        driver.start(20);
        const auto name = ui::requestGitBranchName(&owner, repository.path());
        driver.stop();
        timeout.stop();
        QVERIFY(!timedOut);
        QVERIFY2(
            duplicateBlocked,
            qPrintable(QString("phase=%1 accepted=%2").arg(phase).arg(name.value_or("cancelled"))));
        QVERIFY(name.has_value());
        QCOMPARE(*name, QString("feature-new"));
    }
    void compactHistoryGraphAcrossThemes() {
        ui::GitHistoryWidget history;
        history.resize(350, 300);
        history.show();
        QVector<workspace::GitCommit> commits;
        for (int i = 0; i < 8; ++i) {
            workspace::GitCommit commit;
            commit.id = QString(40, QChar('a' + i));
            if (i != 7)
                commit.parents.append(QString(40, QChar('a' + i + 1)));
            commit.subject = i == 0 ? "Polish mobile responsive UI and improve navigation"
                                    : "Add client-side query caching and refine API handling";
            commit.author = "Atharv Navale";
            if (i == 0)
                commit.references = "HEAD -> refs/heads/main, refs/remotes/origin/main";
            commits.append(commit);
        }
        auto* git = history.findChild<workspace::GitService*>();
        git->historyReady(commits);
        auto* tree = history.findChild<QTreeWidget*>("gitHistory");
        QCOMPARE(tree->topLevelItemCount(), commits.size());
        for (bool dark : {false, true}) {
            ui::Theme::apply(*qApp, dark);
            for (int i = 0; i < commits.size(); ++i)
                QCOMPARE(tree->visualItemRect(tree->topLevelItem(i)).height(), 22);
            QVERIFY(history.grab().save(
                QDir(QCoreApplication::applicationDirPath())
                    .filePath(dark ? "compact-graph-dark.png" : "compact-graph-light.png")));
            history.resize(180, 300);
            QTest::qWait(10);
            QCOMPARE(tree->horizontalScrollBar()->maximum(), 0);
            history.resize(350, 300);
        }
    }
    void quickInputKeyboardAndCancellation() {
        QWidget owner;
        owner.resize(800, 600);
        owner.show();
        ui::components::QuickInputDialog input("Create Branch", "Branch name", {}, &owner);
        input.show();
        auto* field = input.findChild<QLineEdit*>("gitInput");
        QVERIFY(field);
        QVERIFY(input.windowFlags() & Qt::FramelessWindowHint);
        QTest::keyClick(field, Qt::Key_Return);
        QVERIFY(input.isVisible()); // Empty names never submit.
        QTest::keyClicks(field, "feature/test");
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(input.result(), int(QDialog::Accepted));
        QCOMPARE(input.value(), QString("feature/test"));
        ui::components::QuickInputDialog cancelled("Create Branch", "Branch name", {}, &owner);
        cancelled.show();
        QTest::keyClick(cancelled.findChild<QLineEdit*>("gitInput"), Qt::Key_Escape);
        QCOMPARE(cancelled.result(), int(QDialog::Rejected));
        QVERIFY(!cancelled.isVisible());
    }
    void initTestCase() {
        // The shipped application uses Fusion; native Windows style produces
        // different menu chrome and is not a valid visual baseline.
        qApp->setStyle(QStyleFactory::create("Fusion"));
    }
    void modernThemeSemanticColors() {
        // Pin VS Code Modern's roles independently of our implementation so that
        // shared controls cannot accidentally revert to custom/system colors.
        for (bool dark : {false, true, false}) {
            ui::Theme::apply(*qApp, dark);
            const auto theme = ui::Theme::tokens(dark);
            QCOMPARE(theme.appSurface, QColor(dark ? "#181818" : "#f8f8f8"));
            QCOMPARE(theme.editorSurface, QColor(dark ? "#1f1f1f" : "#ffffff"));
            QCOMPARE(theme.foreground, QColor(dark ? "#cccccc" : "#3b3b3b"));
            QCOMPARE(theme.menuSurface, QColor(dark ? "#1f1f1f" : "#ffffff"));
            QCOMPARE(theme.inactiveTabForeground, QColor(dark ? "#9d9d9d" : "#616161"));
            QCOMPARE(theme.activeTabForeground, QColor(dark ? "#ffffff" : "#3b3b3b"));
            QCOMPARE(theme.placeholderForeground, QColor(dark ? "#989898" : "#767676"));
            QCOMPARE(theme.primaryButtonHover, QColor(dark ? "#026ec1" : "#0258a8"));
            QCOMPARE(theme.searchFocusBorder, QColor(dark ? "#0078d4" : "#005fb8"));
            QCOMPARE(theme.error, QColor("#f85149"));
            QCOMPARE(theme.activityForeground, QColor(dark ? "#d7d7d7" : "#1f1f1f"));
            QCOMPARE(theme.activityInactiveForeground, QColor(dark ? "#868686" : "#616161"));
            QCOMPARE(theme.badgeBackground, QColor(dark ? "#616161" : "#cccccc"));
            QCOMPARE(theme.menuSelectionBackground, QColor(dark ? "#2a2d2e" : "#f2f2f2"));
            QCOMPARE(theme.menuSelectionForeground, theme.foreground);
            const auto components = ui::components::resolveComponentTokens(theme);
            QCOMPARE(components.menu.disabledForeground,
                     dark ? QColor(204, 204, 204, 128) : QColor(97, 97, 97, 128));
            QCOMPARE(components.menu.separatorBackground,
                     dark ? QColor(204, 204, 204, 51) : QColor(59, 59, 59, 51));
            QCOMPARE(qApp->palette().color(QPalette::WindowText), theme.foreground);
            QCOMPARE(qApp->palette().color(QPalette::PlaceholderText), theme.placeholderForeground);
            QCOMPARE(qApp->palette().color(QPalette::Base), theme.inputSurface);
            QVERIFY(qApp->styleSheet().contains("color: " + theme.inactiveTabForeground.name()));
            QVERIFY(!qApp->styleSheet().contains("@"));
        }
    }

    void sharedDropdownStyleAndInteraction() {
        for (bool dark : {false, true}) {
            ui::Theme::apply(*qApp, dark);
            QWidget host;
            host.resize(300, 160);
            ui::components::ComboBox combo(&host);
            combo.setGeometry(8, 8, 200, 26);
            combo.addItems({"First", "Unavailable", "Third"});
            auto* model = qobject_cast<QStandardItemModel*>(combo.model());
            QVERIFY(model);
            model->item(1)->setEnabled(false);
            host.show();
            combo.setFocus();
            QCOMPARE(combo.property("litecodeChevronResource").toString(),
                     QString(":/icons/chevron-down.svg"));
            QSignalSpy changed(&combo, &QComboBox::currentIndexChanged);
            QTest::keyClick(&combo, Qt::Key_Space);
            auto* popup = combo.popupWidget();
            auto* list = popup->findChild<QListView*>();
            QVERIFY(list);
            QTRY_VERIFY(popup->isVisible());
            QVERIFY(popup->width() >= combo.width());
            QCOMPARE(list->visualRect(model->index(0, 0)).height(), 22);
            QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                              list->visualRect(model->index(1, 0)).center());
            QVERIFY(popup->isVisible());
            QCOMPARE(combo.currentIndex(), 0);
            QTest::keyClick(list, Qt::Key_End);
            QVERIFY(popup->grab().save(
                QDir(QCoreApplication::applicationDirPath())
                    .filePath(dark ? "dropdown-dark.png" : "dropdown-light.png")));
            QTest::keyClick(list, Qt::Key_Escape);
            QVERIFY(!popup->isVisible());
            QCOMPARE(combo.currentIndex(), 0);
            QCOMPARE(changed.size(), 0);
            QTest::keyClick(&combo, Qt::Key_Down, Qt::AltModifier);
            QTRY_VERIFY(popup->isVisible());
            QTest::keyClick(list, Qt::Key_End);
            QTest::keyClick(list, Qt::Key_Return);
            QTRY_VERIFY(!popup->isVisible());
            QCOMPARE(combo.currentIndex(), 2);
            QCOMPARE(changed.size(), 1);
        }
    }
    void nativeMenuWidthAndSelection() {
        for (bool dark : {false, true}) {
            ui::Theme::apply(*qApp, dark);
            QMainWindow workbenchParent;
            workbenchParent.setObjectName("mainWindow");
            workbenchParent.setStyle(ui::sharedWorkbenchStyle());
            ui::components::Menu menu(&workbenchParent);
            QVERIFY(menu.windowFlags().testFlag(Qt::NoDropShadowWindowHint));
            QActionGroup group(&menu);
            auto* changes = menu.addAction("Changes");
            auto* graph = menu.addAction("Graph");
            for (auto* action : {changes, graph}) {
                action->setCheckable(true);
                group.addAction(action);
            }
            changes->setChecked(true);
            menu.addSeparator();
            menu.addAction("Unavailable")->setEnabled(false);
            menu.popup(QPoint(30, 30));
            QTRY_VERIFY(menu.isVisible());
            QVERIFY(menu.width() >= 160);
            menu.setActiveAction(changes);
            QTest::qWait(20);
            const auto image = menu.grab().toImage();
            const auto row = menu.actionGeometry(changes);
            QCOMPARE(image.pixelColor(menu.actionGeometry(graph).right() - 10,
                                      menu.actionGeometry(graph).center().y()),
                     ui::Theme::tokens(dark).menuSurface);
            QCOMPARE(image.pixelColor(row.right() - 10, row.center().y()),
                     ui::Theme::tokens(dark).menuSelectionBackground);
            QVERIFY(menu.grab().save(QDir(QCoreApplication::applicationDirPath())
                                         .filePath(dark ? "menu-dark.png" : "menu-light.png")));
            graph->trigger();
            QVERIFY(graph->isChecked() && !changes->isChecked());
            menu.close();
        }
    }
    void graphViewOpensHistoricalDiff() {
        QTemporaryDir repository;
        QProcess git;
        git.setWorkingDirectory(repository.path());
        auto run = [&git](const QStringList& args) {
            git.start("git", args);
            return git.waitForFinished(10000) && git.exitCode() == 0;
        };
        QVERIFY(run({"init", "--initial-branch=main"}));
        QVERIFY(run({"config", "user.name", "UI Test"}));
        QVERIFY(run({"config", "user.email", "ui@example.invalid"}));
        QVERIFY(QDir(repository.path()).mkpath("src"));
        QFile file(repository.filePath("src/example.cpp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("int value = 1;\n");
        file.close();
        QVERIFY(run({"add", "."}));
        QVERIFY(run({"-c", "commit.gpgsign=false", "commit", "-m", "Initial example"}));
        ui::SourceControlPanel panel;
        ui::EditorArea editor;
        panel.resize(380, 500);
        panel.show();
        connect(&panel, &ui::SourceControlPanel::comparisonRequested, &editor,
                &ui::EditorArea::openGitComparison);
        QSignalSpy comparisons(&panel, &ui::SourceControlPanel::comparisonRequested);
        panel.setWorkspace(repository.path());
        auto* graph = panel.findChild<QAction*>("scmHistoryView");
        auto* changes = panel.findChild<QAction*>("scmChangesView");
        QVERIFY(graph && changes);
        graph->trigger();
        QVERIFY(graph->isChecked() && !changes->isChecked());
        auto* tree = panel.findChild<QTreeWidget*>("gitHistory");
        QTRY_COMPARE(tree->topLevelItemCount(), 1);
        auto* commit = tree->topLevelItem(0);
        QCOMPARE(tree->visualItemRect(commit).height(), 22);
        QCOMPARE(panel.findChild<QLabel*>("scmRepository")->text(), QString("Graph"));
        QCOMPARE(commit->text(0), QString("Initial example"));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualItemRect(commit).center());
        QTRY_COMPARE(commit->childCount(), 1);
        QCOMPARE(tree->visualItemRect(commit->child(0)).height(), 22);
        QVERIFY(commit->isExpanded());
        QTRY_VERIFY(tree->visualItemRect(commit->child(0)).top() >
                    tree->visualItemRect(commit).bottom());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualItemRect(commit->child(0)).center());
        QTRY_COMPARE(comparisons.size(), 1);
        const auto snapshot = qvariant_cast<workspace::GitComparison>(comparisons.first()[0]);
        QVERIFY(snapshot.historical && snapshot.staged);
        auto* diff = editor.findChild<ui::DiffEditorWidget*>();
        QVERIFY(diff && diff->inlineMode());
        QVERIFY(!diff->findChild<QWidget*>("toggleInlineDiff"));
        auto* tabs = editor.findChild<litecode::ui::components::WorkbenchTabs*>("editorTabs");
        QVERIFY(tabs->tabText(0).contains(snapshot.modifiedRevision.left(7)));
        QVERIFY(tabs->tabText(0).startsWith("example.cpp ("));
        QVERIFY(!tabs->tabText(0).contains("src/"));
        for (bool dark : {false, true}) {
            ui::Theme::apply(*qApp, dark);
            QVERIFY(panel.grab().save(QDir(QCoreApplication::applicationDirPath())
                                          .filePath(dark ? "graph-dark.png" : "graph-light.png")));
            QCOMPARE(tree->horizontalScrollBar()->maximum(), 0);
        }
        changes->trigger();
        QVERIFY(changes->isChecked() && !graph->isChecked());
        QVERIFY(panel.findChild<QTreeWidget*>("gitChanges")->isVisible());
    }
    void untrackedClickOpensInlineAddition() {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("Git not installed");
        QTemporaryDir repository;
        QProcess git;
        git.setWorkingDirectory(repository.path());
        git.start("git", {"init"});
        QVERIFY(git.waitForFinished(10000));
        QCOMPARE(git.exitCode(), 0);
        QFile file(repository.filePath("new.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"status\":\"failed\"}\n");
        file.close();
        ui::SourceControlPanel panel;
        ui::EditorArea editor;
        panel.resize(320, 500);
        panel.show();
        QSignalSpy comparisons(&panel, &ui::SourceControlPanel::comparisonRequested);
        QSignalSpy plainFiles(&panel, &ui::SourceControlPanel::openFileRequested);
        connect(&panel, &ui::SourceControlPanel::comparisonRequested, &editor,
                &ui::EditorArea::openGitComparison);
        panel.setWorkspace(repository.path());
        auto* tree = panel.findChild<QTreeWidget*>("gitChanges");
        QVERIFY(tree);
        QTRY_VERIFY(tree->findItems("new.json", Qt::MatchExactly | Qt::MatchRecursive).size() == 1);
        auto* item = tree->findItems("new.json", Qt::MatchExactly | Qt::MatchRecursive).first();
        QCOMPARE(item->data(0, Qt::UserRole + 4).toString(), QString("U"));
        auto* group = item->parent();
        QVERIFY(group && group->text(0).startsWith("Changes"));
        const auto groupCenter = tree->visualItemRect(group).center();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, groupCenter);
        QVERIFY(!group->isExpanded());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, groupCenter);
        QVERIFY(group->isExpanded());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                          tree->visualItemRect(item).center());
        QTRY_COMPARE(comparisons.size(), 1);
        QCOMPARE(plainFiles.size(), 0);
        auto* diff = editor.findChild<ui::DiffEditorWidget*>();
        QVERIFY(diff && diff->inlineMode());
        QTRY_COMPARE(diff->hunks().size(), 1);
        QCOMPARE(diff->hunks()[0].originalCount, 0);
        QCOMPARE(diff->hunks()[0].modifiedCount, 1);
    }
    void refreshedDiffReplacesExistingTab() {
        ui::EditorArea editor;
        auto* before = new QWidget;
        editor.openAuxiliaryTab("git-diff:test:working", "First", before, true);
        auto* after = new QWidget;
        editor.openAuxiliaryTab("git-diff:test:working", "Updated", after, true);
        auto* tabs = editor.findChild<litecode::ui::components::WorkbenchTabs*>("editorTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(tabs->currentWidget(), after);
        QCOMPARE(tabs->tabText(0), QString("Updated"));
        editor.openAuxiliaryTab("git-diff:test:staged", "Staged", new QWidget, true);
        QCOMPARE(tabs->count(), 2);
        editor.openAuxiliaryTab("git-diff:test:working", "Ignored", new QWidget);
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentWidget(), after);
    }
    void integratedSidebarAndThemes() {
        QTemporaryDir directory;
        core::SettingsService settings(directory.filePath("settings.ini"));
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
        window.resize(1200, 800);
        window.show();
        auto* sidebar = window.findChild<QDockWidget*>("workspaceDock");
        auto* pages = sidebar ? sidebar->findChild<QStackedWidget*>("sidebarContainers") : nullptr;
        auto* sourceAction = window.findChild<QAction*>("sourceControlAction");
        auto* extensionAction = window.findChild<QAction*>("extensionsAction");
        auto* rail = window.findChild<QToolBar*>("activityBar");
        QVERIFY(pages && sidebar && sourceAction && extensionAction && rail);
        QCOMPARE(pages->count(), 5);
        QVERIFY(!window.findChild<QDockWidget*>("sourceControlDock"));
        QVERIFY(!window.findChild<QDockWidget*>("extensionsDock"));
        QVERIFY(rail->actions().indexOf(sourceAction) < rail->actions().indexOf(extensionAction));
        const int width = sidebar->width();
        for (const bool dark : {false, true}) {
            // Exercise MainWindow's real settings/theme refresh path, including
            // cached toolbar icons and caption palette, not just global QSS.
            settings.saveTheme(dark ? "dark" : "light");
            QVERIFY(settings.sync());
            QTRY_COMPARE(qApp->property("litecodeDarkTheme").toBool(), dark);
            sourceAction->trigger();
            QTRY_COMPARE(pages->currentIndex(), 2);
            QCOMPARE(sidebar->width(), width);
            QVERIFY(sourceAction->isChecked());
            QVERIFY(!extensionAction->isChecked());
            QCOMPARE(sidebar->titleBarWidget()->findChild<QLabel*>("dockHeaderTitle")->text(),
                     QString("Source Control"));
            auto* panel = window.findChild<ui::SourceControlPanel*>();
            QVERIFY(panel && panel->isVisible());
            QTest::qWait(30);
            const auto sourceButton = rail->widgetForAction(sourceAction)->grab().toImage();
            // A borderless rail button is transparent over the toolbar surface.
            QCOMPARE(sourceButton.pixelColor(0, sourceButton.height() / 2).alpha(), 0);
            QCOMPARE(sourceButton.pixelColor(sourceButton.width() - 1,
                                            sourceButton.height() / 2).alpha(), 0);
            QCOMPARE(rail->widgetForAction(sourceAction)->width(),
                     rail->widgetForAction(rail->actions().first())->width());
            QCOMPARE(panel->grab().toImage().pixelColor(2, 2),
                     ui::Theme::tokens(dark).sidebarSurface);
            auto* input = panel->findChild<QPlainTextEdit*>("gitCommitMessage");
            QCOMPARE(input->viewport()->grab().toImage().pixelColor(2, 2),
                     ui::Theme::tokens(dark).inputSurface);
            auto* sharedHeader = sidebar->titleBarWidget();
            const auto headerHeight = sharedHeader->height();
            QVERIFY(headerHeight > 0);
            panel->findChild<QAction*>("scmHistoryView")->trigger();
            QCOMPARE(sidebar->titleBarWidget(), sharedHeader);
            QCOMPARE(sharedHeader->height(), headerHeight);
            QVERIFY(sharedHeader->isVisible());
            QCOMPARE(sidebar->titleBarWidget()->findChild<QLabel*>("dockHeaderTitle")->text(),
                     QString("Source Control"));
            QCOMPARE(panel->findChild<QLabel*>("scmRepository")->accessibleName(),
                     QString("Graph"));
            panel->findChild<QAction*>("scmChangesView")->trigger();
            QCOMPARE(sidebar->titleBarWidget(), sharedHeader);
            QCOMPARE(sharedHeader->height(), headerHeight);
            panel->findChild<QAction*>("scmHistoryView")->trigger();
            extensionAction->trigger();
            QTRY_COMPARE(pages->currentIndex(), 3);
            QCOMPARE(sidebar->width(), width);
            QVERIFY(sidebar->titleBarWidget()->height() > 0);
            QVERIFY(extensionAction->isChecked());
            QVERIFY(!sourceAction->isChecked());
            QCOMPARE(sidebar->titleBarWidget()->findChild<QLabel*>("dockHeaderTitle")->text(), QString("Extensions"));
            auto* extensionPanel = window.findChild<ui::ExtensionsPanel*>();
            QTest::qWait(30);
            const auto extensionButton = rail->widgetForAction(extensionAction)->grab().toImage();
            QCOMPARE(extensionButton.pixelColor(0, extensionButton.height() / 2).alpha(), 0);
            QCOMPARE(extensionButton.pixelColor(extensionButton.width() - 1,
                                               extensionButton.height() / 2).alpha(), 0);
            QCOMPARE(rail->widgetForAction(extensionAction)->width(),
                     rail->widgetForAction(rail->actions().first())->width());
            QCOMPARE(extensionPanel->grab().toImage().pixelColor(2, 2),
                     ui::Theme::tokens(dark).sidebarSurface);
            const auto capture = qEnvironmentVariable("LITECODE_SCM_CAPTURE");
            if (!capture.isEmpty()) {
                QTest::qWait(40);
                QVERIFY(window.grab().save(
                    capture + (dark ? "-extensions-dark.png" : "-extensions-light.png")));
                sourceAction->trigger();
                panel->findChild<QAction*>("scmChangesView")->trigger();
                QTest::qWait(40);
                QVERIFY(window.grab().save(capture +
                                           (dark ? "-source-dark.png" : "-source-light.png")));
            }
            panel->findChild<QAction*>("scmChangesView")->trigger();
        }
        extensionAction->trigger();
        window.close();
        QCOMPARE(settings.workbenchLayout().primarySidebarView, 3);
        ui::MainWindow restarted(settings, workspace, {search, quick, replace}, false);
        restarted.show();
        auto* restoredSidebar = restarted.findChild<QDockWidget*>("workspaceDock");
        QVERIFY(restoredSidebar);
        auto* restoredPages = restoredSidebar->findChild<QStackedWidget*>("sidebarContainers");
        QVERIFY(restoredPages);
        QTRY_COMPARE(restoredPages->currentIndex(), 3);
    }
    void compactRowsAndInlineStage() {
        if (QStandardPaths::findExecutable("git").isEmpty())
            QSKIP("Git unavailable");
        QTemporaryDir directory;
        QProcess init;
        init.setWorkingDirectory(directory.path());
        init.start("git", {"init"});
        QVERIFY(init.waitForFinished(10000));
        QCOMPARE(init.exitCode(), 0);
        const QString name = "a very long literal [file] name for truncation.cpp";
        QFile file(directory.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("int value;\n");
        file.close();
        ui::Theme::apply(*qApp, false);
        ui::SourceControlPanel panel;
        panel.resize(270, 600);
        panel.show();
        panel.setWorkspace(directory.path());
        auto* git = panel.findChild<workspace::GitService*>();
        auto* tree = panel.findChild<QTreeWidget*>("gitChanges");
        QTRY_VERIFY(!git->busy() && git->status().changes.size() == 1);
        auto* commitButton = panel.findChild<QPushButton*>("gitCommitButton");
        QVERIFY(commitButton);
        QCOMPARE(commitButton->property("uiComponent").toString(), QString("button"));
        QCOMPARE(commitButton->property("litecodeIconResource").toString(),
                 QString(":/icons/check.svg"));
        QVERIFY(!commitButton->icon().isNull());
        const auto capture = qEnvironmentVariable("LITECODE_SCM_CAPTURE");
        if (!capture.isEmpty()) {
            for (const bool dark : {false, true}) {
                ui::Theme::apply(*qApp, dark);
                QTest::qWait(30);
                QVERIFY(panel.grab().save(capture + (dark ? "-rows-dark.png" : "-rows-light.png")));
            }
        }
        auto* row = tree->topLevelItem(1)->child(0);
        QVERIFY(row);
        QCOMPARE(row->data(0, Qt::UserRole).toString(), name);
        QCOMPARE(tree->visualItemRect(row).height(), 22);
        QCOMPARE(tree->horizontalScrollBar()->maximum(), 0);
        QSignalSpy opened(&panel, &ui::SourceControlPanel::openFileRequested);
        const auto rect = tree->visualItemRect(row);
        QTest::mouseMove(tree->viewport(), QPoint(rect.right() - 36, rect.center().y()));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {},
                          QPoint(rect.right() - 36, rect.center().y()));
        QTRY_VERIFY(!git->busy() && git->status().changes[0].index == 'A');
        QCOMPARE(opened.size(), 0);
        row = tree->topLevelItem(0)->child(0);
        QVERIFY(row);
        const auto stagedRect = tree->visualItemRect(row);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {},
                          QPoint(stagedRect.right() - 36, stagedRect.center().y()));
        QTRY_VERIFY(!git->busy() && git->status().changes[0].index == '?');
    }
};
QTEST_MAIN(SourceControlUiTests)
#include "tst_source_control_ui.moc"
