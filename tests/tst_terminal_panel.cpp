#include "terminal/ITerminalBackend.h"
#include "terminal/VTermEngine.h"
#ifdef Q_OS_WIN
#include "terminal/TerminalSessionService.h"
#include "terminal/WindowsConPtyBackend.h"
#endif
#include "ui/TerminalPanel.h"
#include "ui/TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace {

class RecordingTerminalBackend final : public litecode::terminal::ITerminalBackend {
  public:
    explicit RecordingTerminalBackend(
        const std::shared_ptr<QStringList>& starts,
        std::shared_ptr<QVector<QSize>> initialSizes = {},
        std::shared_ptr<QVector<litecode::terminal::TerminalProfile>> profiles = {},
        std::shared_ptr<QVector<QByteArray>> inputs = {})
        : starts_(starts), initialSizes_(std::move(initialSizes)), profiles_(std::move(profiles)),
          inputs_(std::move(inputs)) {}

    void start(litecode::core::OperationId operationId,
               const litecode::terminal::TerminalProfile& profile,
               const QString& workingDirectory) override {
        operationId_ = operationId;
        starts_->push_back(QDir::cleanPath(workingDirectory));
        if (profiles_)
            profiles_->push_back(profile);
        if (initialSizes_)
            initialSizes_->push_back(size_);
        state_ = litecode::terminal::TerminalState::Running;
        emit stateChanged(operationId_, state_);
        emit started(operationId_);
        if (profile.id == QStringLiteral("debug-terminal"))
            emit outputReady(QByteArrayLiteral("debug> "));
    }

    bool enqueueInput(const QByteArray& bytes) override {
        if (inputs_)
            inputs_->append(bytes);
        return state_ == litecode::terminal::TerminalState::Running;
    }
    void resizeTerminal(const QSize& size) override { size_ = size; }
    void requestStop() override {
        if (state_ == litecode::terminal::TerminalState::Stopped)
            return;
        state_ = litecode::terminal::TerminalState::Stopped;
        emit stateChanged(operationId_, state_);
        emit completed({operationId_, litecode::core::CompletionKind::Cancelled, 0, false, {}});
    }
    litecode::terminal::TerminalState state() const override { return state_; }
    qint64 processId() const override {
        return state_ == litecode::terminal::TerminalState::Running ? 12345 : 0;
    }
    bool shellIsIdle() const override {
        return state_ == litecode::terminal::TerminalState::Running;
    }

  private:
    std::shared_ptr<QStringList> starts_;
    std::shared_ptr<QVector<QSize>> initialSizes_;
    std::shared_ptr<QVector<litecode::terminal::TerminalProfile>> profiles_;
    std::shared_ptr<QVector<QByteArray>> inputs_;
    QSize size_;
    litecode::core::OperationId operationId_;
    litecode::terminal::TerminalState state_{litecode::terminal::TerminalState::Stopped};
};

QString resolvedDirectory(const QString& path) {
    const QFileInfo info(path);
    return QDir::cleanPath(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath()
                                                              : info.canonicalFilePath());
}

} // namespace

class TerminalPanelTest final : public QObject {
    Q_OBJECT

  private slots:
    void defaultShellMatchesSelectedProfileAndOnlyChangesForNewPath() {
        litecode::ui::TerminalPanel panel;
        const auto profiles = litecode::terminal::availableTerminalProfiles();
        QVERIFY(!profiles.isEmpty());
        QCOMPARE(panel.defaultShell(), profiles.first().executable);
        QSignalSpy changed(&panel, &litecode::ui::TerminalPanel::defaultShellChanged);
        panel.setDefaultProfile(profiles.first().id);
        QCOMPARE(changed.size(), 0);
        QString previous = panel.defaultShell();
        for (const auto& profile : profiles) {
            changed.clear();
            panel.setDefaultProfile(profile.id);
            QCOMPARE(panel.defaultShell(), profile.executable);
            QCOMPARE(changed.size(), previous == profile.executable ? 0 : 1);
            if (!changed.isEmpty())
                QCOMPARE(changed.first().first().toString(), profile.executable);
            previous = profile.executable;
        }
        changed.clear();
        panel.setDefaultProfile("not-an-installed-profile");
        QCOMPARE(panel.defaultShell(), profiles.first().executable);
        QCOMPARE(changed.size(), previous == profiles.first().executable ? 0 : 1);
    }
    void taskProcessKeepsLiteralArgumentsAndEnvironment();
    void debugCommandRunsInsidePersistentShell();
    void debugTerminalReusesViewAcrossLaunchEnvironments();
    void canceledDebugCommandNeverSubmitsInput();
    void terminalHereDoesNotChangeFutureTerminalDirectory();
    void hiddenPanelStartsOnlyWhenShown();
    void visiblePanelSetsTerminalSizeBeforeLaunch();
    void manyTerminalsCanBeDeletedWithoutLosingSelection();
    void multilinePasteIsDirect_data();
    void multilinePasteIsDirect();
    void screenClearRevealsLiveViewport_data();
    void screenClearRevealsLiveViewport();
#ifdef Q_OS_WIN
    void debugCommandAcceptsInteractiveInput();
    void cmdFullClearRemovesScrollback();
    void cmdClsClearsVisibleViewport();
#endif
};

void TerminalPanelTest::taskProcessKeepsLiteralArgumentsAndEnvironment() {
    const auto starts = std::make_shared<QStringList>();
    const auto profiles = std::make_shared<QVector<litecode::terminal::TerminalProfile>>();
    litecode::ui::TerminalPanel panel(nullptr, [starts, profiles] {
        return std::make_unique<RecordingTerminalBackend>(starts, nullptr, profiles);
    });
    QSignalSpy completed(&panel, &litecode::ui::TerminalPanel::processCompleted);
    QTemporaryDir directory;
    litecode::terminal::TerminalProfile process;
    process.id = "task:build";
    process.displayName = "Build";
    process.executable = "fixture-compiler";
    process.arguments = {"file with spaces.cpp", "&literal|argument"};
    process.environment.insert("TASK_ENV", "literal value");
    const auto id = panel.runProcess(process, directory.path());
    QVERIFY(id.isValid());
    QTRY_COMPARE(profiles->size(), 1);
    QCOMPARE(profiles->first().executable, process.executable);
    QCOMPARE(profiles->first().arguments, process.arguments);
    QCOMPARE(profiles->first().environment.value("TASK_ENV"), QString("literal value"));
    QCOMPARE(starts->first(), QDir::cleanPath(directory.path()));
    panel.stopProcess(id);
    QTRY_COMPARE(completed.size(), 1);
    QVERIFY(!panel.runProcess({}, directory.path()).isValid());
}

void TerminalPanelTest::debugCommandRunsInsidePersistentShell() {
    const auto starts = std::make_shared<QStringList>();
    const auto profiles = std::make_shared<QVector<litecode::terminal::TerminalProfile>>();
    const auto inputs = std::make_shared<QVector<QByteArray>>();
    litecode::ui::TerminalPanel panel(nullptr, [starts, profiles, inputs] {
        return std::make_unique<RecordingTerminalBackend>(starts, nullptr, profiles, inputs);
    });
    QTemporaryDir directory;
    auto environment = litecode::terminal::refreshedProcessEnvironment();
    QSignalSpy ready(&panel, &litecode::ui::TerminalPanel::debugCommandReady);
    environment.insert(QStringLiteral("DEBUG_TEST"), QStringLiteral("value"));
    const auto id = panel.runDebugCommand(
        QStringLiteral("Debug Console"),
        {QStringLiteral("debuggee.exe"), QStringLiteral("file with spaces.py")}, environment,
        directory.path());
    QVERIFY(id.isValid());
    QVERIFY(ready.isEmpty());
    QTRY_COMPARE(profiles->size(), 1);
    QCOMPARE(profiles->first().id, QStringLiteral("debug-terminal"));
    QVERIFY(profiles->first().executable != QStringLiteral("debuggee.exe"));
    QCOMPARE(profiles->first().environment.value(QStringLiteral("DEBUG_TEST")),
             QStringLiteral("value"));
    QTRY_COMPARE_WITH_TIMEOUT(inputs->size(), 1, 2000);
    QVERIFY(inputs->first().contains("debuggee.exe"));
    QVERIFY(inputs->first().contains("file with spaces.py"));
    QVERIFY(inputs->first().endsWith('\r'));
    QCOMPARE(ready.size(), 1);
    QCOMPARE(ready.first().at(1).toLongLong(), 12345);
    QVERIFY(ready.first().at(2).toString().isEmpty());
    panel.stopProcess(id);
}

void TerminalPanelTest::debugTerminalReusesViewAcrossLaunchEnvironments() {
    const auto starts = std::make_shared<QStringList>();
    const auto profiles = std::make_shared<QVector<litecode::terminal::TerminalProfile>>();
    litecode::ui::TerminalPanel panel(nullptr, [starts, profiles] {
        return std::make_unique<RecordingTerminalBackend>(starts, nullptr, profiles);
    });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSignalSpy ready(&panel, &litecode::ui::TerminalPanel::debugCommandReady);
    QProcessEnvironment environment;
    const auto run = [&] {
        return panel.runDebugCommand("Debug", {"debuggee"}, environment, directory.path());
    };
    const auto first = run();
    QVERIFY(first.isValid());
    QTRY_COMPARE(ready.size(), 1);
    auto* view = panel.findChild<litecode::ui::TerminalView*>();
    QVERIFY(view);
    panel.releaseDebugTerminal(first);
    QCOMPARE(run(), first);
    QTRY_COMPARE(ready.size(), 2);
    QCOMPARE(profiles->size(), 1);

    panel.releaseDebugTerminal(first);
    environment.insert("DEBUG_TEST", "changed");
    const auto replaced = run();
    QVERIFY(replaced.isValid() && replaced != first);
    QTRY_COMPARE(ready.size(), 3);
    QCOMPARE(profiles->last().environment.value("DEBUG_TEST"), QStringLiteral("changed"));
    QCOMPARE(panel.findChildren<litecode::ui::TerminalView*>(), QList{view});

    const auto separate = run();
    QVERIFY(separate.isValid() && separate != replaced);
    QCOMPARE(panel.findChildren<litecode::ui::TerminalView*>().size(), 2);
    QTRY_COMPARE(ready.size(), 4);
}

void TerminalPanelTest::canceledDebugCommandNeverSubmitsInput() {
    const auto starts = std::make_shared<QStringList>();
    const auto inputs = std::make_shared<QVector<QByteArray>>();
    litecode::ui::TerminalPanel panel(nullptr, [starts, inputs] {
        return std::make_unique<RecordingTerminalBackend>(starts, nullptr, nullptr, inputs);
    });
    QTemporaryDir directory;
    QSignalSpy ready(&panel, &litecode::ui::TerminalPanel::debugCommandReady);
    const auto id =
        panel.runDebugCommand("Debug", {"never-run"},
                              litecode::terminal::refreshedProcessEnvironment(), directory.path());
    QVERIFY(id.isValid());
    panel.cancelDebugCommand(id);
    QCOMPARE(ready.size(), 1);
    QVERIFY(!ready.first().at(2).toString().isEmpty());
    QTest::qWait(600);
    QVERIFY(inputs->isEmpty());
    QCOMPARE(ready.size(), 1);
}

#ifdef Q_OS_WIN
void TerminalPanelTest::debugCommandAcceptsInteractiveInput() {
    QTemporaryDir directory;
    litecode::ui::TerminalPanel panel;
    panel.setDefaultProfile("command-prompt");
    panel.resize(1000, 400);
    auto environment = litecode::terminal::refreshedProcessEnvironment();
    environment.insert("LITECODE_DEBUG_ENV", "debug-value");
    QSignalSpy ready(&panel, &litecode::ui::TerminalPanel::debugCommandReady);
    QSignalSpy completed(&panel, &litecode::ui::TerminalPanel::processCompleted);
    const auto script = QFINDTESTDATA("fixtures/InteractiveTerminal.cmd");
    QVERIFY(!script.isEmpty());
    const auto id =
        panel.runDebugCommand("Interactive debug target", {script}, environment, directory.path());
    QVERIFY(id.isValid());
    panel.show();
    auto* view = panel.findChild<litecode::ui::TerminalView*>();
    QVERIFY(view);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 10000);
    QVERIFY2(ready.first().at(2).toString().isEmpty(), qPrintable(ready.first().at(2).toString()));
    QVERIFY(ready.first().at(1).toLongLong() > 0);
    QTRY_VERIFY_WITH_TIMEOUT(view->plainText().contains("Input:"), 10000);
    QTest::keyClicks(view, "hello");
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_VERIFY2_WITH_TIMEOUT(view->plainText().contains("RESULT:hello:debug-value"),
                              qPrintable(view->plainText()), 10000);
    // The target finishing must not destroy the persistent terminal shell.
    QTest::keyClicks(view, "echo SHELL_STILL_ALIVE");
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(view->plainText().count("SHELL_STILL_ALIVE") >= 2, 10000);
    panel.stopProcess(id);
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
}
#endif

void TerminalPanelTest::terminalHereDoesNotChangeFutureTerminalDirectory() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString workspace = temporary.filePath(QStringLiteral("workspace"));
    const QString scripts = QDir(workspace).filePath(QStringLiteral("scripts"));
    QVERIFY(QDir().mkpath(scripts));

    const auto starts = std::make_shared<QStringList>();
    litecode::ui::TerminalPanel panel(
        nullptr, [starts] { return std::make_unique<RecordingTerminalBackend>(starts); });
    panel.setWorkingDirectory(workspace);
    panel.show();
    QTRY_COMPARE_WITH_TIMEOUT(starts->size(), 1, 2'000);

    panel.openInDirectory(scripts);
    QTRY_COMPARE_WITH_TIMEOUT(starts->size(), 2, 2'000);
    panel.newTerminal();
    QTRY_COMPARE_WITH_TIMEOUT(starts->size(), 3, 2'000);

    QCOMPARE(starts->at(0), resolvedDirectory(workspace));
    QCOMPARE(starts->at(1), resolvedDirectory(scripts));
    QCOMPARE(starts->at(2), resolvedDirectory(workspace));
}

void TerminalPanelTest::hiddenPanelStartsOnlyWhenShown() {
    const auto starts = std::make_shared<QStringList>();
    litecode::ui::TerminalPanel panel(
        nullptr, [starts] { return std::make_unique<RecordingTerminalBackend>(starts); });

    QCoreApplication::processEvents();
    QVERIFY(starts->isEmpty());
    QVERIFY(!panel.hasTerminals());
    panel.show();
    QTRY_COMPARE_WITH_TIMEOUT(starts->size(), 1, 2'000);
    QVERIFY(panel.hasTerminals());
}

void TerminalPanelTest::visiblePanelSetsTerminalSizeBeforeLaunch() {
    const auto starts = std::make_shared<QStringList>();
    const auto initialSizes = std::make_shared<QVector<QSize>>();
    litecode::ui::TerminalPanel panel(nullptr, [starts, initialSizes] {
        return std::make_unique<RecordingTerminalBackend>(starts, initialSizes);
    });
    panel.resize(900, 400);
    panel.show();
    QTRY_COMPARE_WITH_TIMEOUT(initialSizes->size(), 1, 2'000);

    auto* view = panel.findChild<litecode::ui::TerminalView*>();
    QVERIFY(view);
    QVERIFY(initialSizes->first().width() >= 20);
    QVERIFY(initialSizes->first().height() >= 2);
    QCOMPARE(initialSizes->first(), view->terminalSize());
}

void TerminalPanelTest::manyTerminalsCanBeDeletedWithoutLosingSelection() {
    const auto starts = std::make_shared<QStringList>();
    litecode::ui::TerminalPanel panel(
        nullptr, [starts] { return std::make_unique<RecordingTerminalBackend>(starts); });
    panel.resize(900, 400);
    panel.show();
    QTRY_COMPARE_WITH_TIMEOUT(starts->size(), 1, 2'000);
    panel.newTerminal();
    panel.newTerminal();
    panel.newTerminal();
    QTRY_COMPARE_WITH_TIMEOUT(starts->size(), 4, 2'000);

    auto* list = panel.findChild<QListWidget*>(QStringLiteral("terminalSessionList"));
    QVERIFY(list);
    QCOMPARE(list->count(), 4);
    QCOMPARE(list->currentRow(), 3);

    // Kill an inactive terminal through the same inline action used by the UI.
    const QRect firstRow = list->visualItemRect(list->item(0));
    QVERIFY(firstRow.isValid());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(firstRow.right() - 10, firstRow.center().y()));
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 3, 2'000);
    QCOMPARE(list->currentRow(), 2);

    // Delete operates on the selected terminal and leaves a valid neighbouring selection.
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Delete);
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 2, 2'000);
    QCOMPARE(list->currentRow(), 1);
    QTest::keyClick(list, Qt::Key_Delete);
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 2'000);
    QVERIFY(list->isHidden());

    QSignalSpy closeRequested(&panel, &litecode::ui::TerminalPanel::closePanelRequested);
    QToolButton* killButton = nullptr;
    for (QToolButton* button : panel.findChildren<QToolButton*>()) {
        if (button->toolTip() == QStringLiteral("Kill Terminal")) {
            killButton = button;
            break;
        }
    }
    QVERIFY(killButton);
    QVERIFY(killButton->isVisible());
    QTest::mouseClick(killButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!panel.hasTerminals(), 2'000);
    QCOMPARE(closeRequested.size(), 1);
}

void TerminalPanelTest::multilinePasteIsDirect_data() {
    QTest::addColumn<QString>("clipboardText");
    QTest::newRow("line-feed") << QStringLiteral("first command\nsecond command");
    QTest::newRow("crlf") << QStringLiteral("first command\r\nsecond command");
    QTest::newRow("carriage-return") << QStringLiteral("first command\rsecond command");
}

void TerminalPanelTest::multilinePasteIsDirect() {
    QFETCH(QString, clipboardText);
    litecode::ui::TerminalView view;
    QSignalSpy output(view.engine(), &litecode::terminal::VTermEngine::outputGenerated);
    QClipboard* clipboard = QApplication::clipboard();
    const QString previousClipboardText = clipboard->text();
    const auto restoreClipboard = qScopeGuard(
        [clipboard, previousClipboardText] { clipboard->setText(previousClipboardText); });
    clipboard->setText(clipboardText);

    QTest::keyClick(&view, Qt::Key_V, Qt::ControlModifier);

    QVERIFY(!QApplication::activeModalWidget());
    QVERIFY(output.size() > 0);
}

void TerminalPanelTest::screenClearRevealsLiveViewport_data() {
    QTest::addColumn<QByteArray>("clearOutput");
    QTest::newRow("erase-display") << QByteArray("\x1b[2J\x1b[H");
    QTest::newRow("cmd-cls") << QByteArray("\x1b[H\x1b[K\r\n\x1b[K");
}

void TerminalPanelTest::screenClearRevealsLiveViewport() {
    QFETCH(QByteArray, clearOutput);
    litecode::ui::TerminalView view;
    view.feed(QByteArray("old output\r\n").repeated(view.engine()->rows() + 8));
    QScrollBar* scrollBar = view.verticalScrollBar();
    QVERIFY(scrollBar->maximum() > 0);
    scrollBar->setValue(0);

    // ConPTY may split the home and erase sequences across output callbacks.
    const int split = clearOutput.indexOf('\x1b', 1);
    QVERIFY(split > 0);
    view.feed(clearOutput.left(split));
    view.feed(clearOutput.mid(split));

    QCOMPARE(scrollBar->value(), scrollBar->maximum());
    scrollBar->setValue(0);
    view.feed(QByteArrayLiteral("ordinary output"));
    QCOMPARE(scrollBar->value(), 0);
}

#ifdef Q_OS_WIN
void TerminalPanelTest::cmdFullClearRemovesScrollback() {
    litecode::ui::TerminalView view;
    const int rows = view.engine()->rows();
    view.feed(QByteArray("OLD_BANNER\r\n").repeated(rows + 8));
    QVERIFY(view.engine()->historyLineCount() > 0);

    view.feed(QByteArrayLiteral("\x1b[H\x1b[K\r\n\x1b[Kpartial redraw"));
    QVERIFY(view.engine()->historyLineCount() > 0);

    QByteArray clearOutput = QByteArrayLiteral("\x1b[H");
    for (int row = 0; row < rows; ++row)
        clearOutput += QByteArrayLiteral("\x1b[K\r\n");
    const int midpoint = clearOutput.size() / 2;
    view.feed(clearOutput.left(midpoint));
    view.feed(clearOutput.mid(midpoint));
    QCOMPARE(view.engine()->historyLineCount(), 0);
    QCOMPARE(view.verticalScrollBar()->value(), 0);
    for (int row = 0; row < rows; ++row)
        QVERIFY(!view.engine()
                     ->lineText(view.engine()->historyLineCount() + row)
                     .contains(QStringLiteral("OLD_BANNER")));
}
#endif

#ifdef Q_OS_WIN
void TerminalPanelTest::cmdClsClearsVisibleViewport() {
    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());
    litecode::ui::TerminalView view;
    view.resize(900, 500);
    view.show();
    QCoreApplication::processEvents();

    litecode::terminal::TerminalSessionService sessions(
        nullptr, [] { return std::make_unique<litecode::terminal::WindowsConPtyBackend>(); });
    const auto commandPrompt = std::find_if(
        sessions.profiles().cbegin(), sessions.profiles().cend(),
        [](const auto& profile) { return profile.id == QStringLiteral("command-prompt"); });
    QVERIFY(commandPrompt != sessions.profiles().cend());
    QByteArray output;
    connect(&sessions, &litecode::terminal::TerminalSessionService::outputReady, &view,
            [&view, &output](litecode::core::OperationId, const QByteArray& bytes) {
                output += bytes;
                view.feed(bytes);
            });
    connect(&sessions, &litecode::terminal::TerminalSessionService::sessionStarted, &view,
            [&sessions, &view](litecode::core::OperationId id) {
                sessions.resizeSession(id, view.terminalSize());
            });
    const auto session = sessions.createSession(commandPrompt->id, workingDirectory.path());
    QTRY_VERIFY_WITH_TIMEOUT(view.plainText().contains(QStringLiteral("Microsoft Windows")),
                             10'000);
    QVERIFY(sessions.sendInput(session,
                               QByteArrayLiteral("for /L %i in (1,1,40) do @echo OLD_BANNER\r")));
    QTRY_VERIFY_WITH_TIMEOUT(view.engine()->historyLineCount() > 0, 10'000);
    output.clear();
    QVERIFY(sessions.sendInput(session, QByteArrayLiteral("cls\r")));
    QTRY_VERIFY_WITH_TIMEOUT(output.contains(QByteArrayLiteral("\x1b[H\x1b[K")), 10'000);
    QTRY_COMPARE_WITH_TIMEOUT(view.engine()->historyLineCount(), 0, 10'000);

    const int firstVisibleLine = view.verticalScrollBar()->value();
    QString visibleText;
    for (int row = 0; row < view.engine()->rows(); ++row)
        visibleText += view.engine()->lineText(firstVisibleLine + row) + QLatin1Char('\n');
    QVERIFY2(!visibleText.contains(QStringLiteral("Microsoft Windows")), qPrintable(visibleText));
    sessions.requestRemove(session);
}
#endif

QTEST_MAIN(TerminalPanelTest)

#include "tst_terminal_panel.moc"
