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
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace {

class RecordingTerminalBackend final : public litecode::terminal::ITerminalBackend {
  public:
    explicit RecordingTerminalBackend(const std::shared_ptr<QStringList>& starts,
                                      std::shared_ptr<QVector<QSize>> initialSizes = {})
        : starts_(starts), initialSizes_(std::move(initialSizes)) {}

    void start(litecode::core::OperationId operationId, const litecode::terminal::TerminalProfile&,
               const QString& workingDirectory) override {
        operationId_ = operationId;
        starts_->push_back(QDir::cleanPath(workingDirectory));
        if (initialSizes_)
            initialSizes_->push_back(size_);
        state_ = litecode::terminal::TerminalState::Running;
        emit stateChanged(operationId_, state_);
        emit started(operationId_);
    }

    bool enqueueInput(const QByteArray&) override {
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

  private:
    std::shared_ptr<QStringList> starts_;
    std::shared_ptr<QVector<QSize>> initialSizes_;
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
    void terminalHereDoesNotChangeFutureTerminalDirectory();
    void hiddenPanelStartsOnlyWhenShown();
    void visiblePanelSetsTerminalSizeBeforeLaunch();
    void manyTerminalsCanBeDeletedWithoutLosingSelection();
    void multilinePasteIsDirect_data();
    void multilinePasteIsDirect();
    void screenClearRevealsLiveViewport_data();
    void screenClearRevealsLiveViewport();
#ifdef Q_OS_WIN
    void cmdFullClearRemovesScrollback();
    void cmdClsClearsVisibleViewport();
#endif
};

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
