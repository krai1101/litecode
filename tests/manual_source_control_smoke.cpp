// Opt-in UI smoke driver. Run explicitly with LITECODE_SCM_MANUAL_REPOSITORY.
// Git subprocesses below only read state; mutations go through workbench UI actions.
#include "core/SettingsService.h"
#include "extensions/ExtensionManager.h"
#include "extensions/WorkbenchOutput.h"
#include "ui/EditorArea.h"
#include "ui/ExtensionsPanel.h"
#include "ui/GitOutputChannel.h"
#include "ui/MainWindow.h"
#include "ui/QuickOpenDialog.h"
#include "ui/SourceControlPanel.h"
#include "ui/Theme.h"
#include "ui/components/QuickInputDialog.h"
#include "ui/components/WorkbenchTabs.h"
#include "ui/controllers/WorkbenchController.h"
#include "ui/extensions/OutputPanel.h"
#include "workspace/GitService.h"
#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"
#include <QAction>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>
using namespace litecode;
namespace {
QString readGit(const QString& root, const QStringList& args) {
    QProcess process;
    process.setWorkingDirectory(root);
    process.start("git", args);
    if (!process.waitForFinished(20000) || process.exitCode())
        return "ERROR: " + QString::fromUtf8(process.readAllStandardError());
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
QString normalized(QString text) { return text.remove('&').replace(QChar(0x2026), "..."); }
QAction* actionIn(QMenu& menu, const QStringList& path, int depth = 0) {
    for (auto* action : menu.actions())
        if (normalized(action->text()) == path[depth]) {
            if (depth + 1 == path.size())
                return action;
            if (action->menu())
                return actionIn(*action->menu(), path, depth + 1);
        }
    return nullptr;
}
bool menu(ui::SourceControlPanel& panel, const QStringList& path) {
    bool triggered = false;
    QTimer::singleShot(0, &panel, [&] {
        auto* popup = panel.findChild<QMenu*>("scmActionsMenu");
        if (!popup)
            return;
        auto* action = actionIn(*popup, path);
        popup->close();
        if (action && action->isEnabled()) {
            triggered = true;
            action->trigger();
        }
    });
    panel.findChild<QToolButton*>("scmMore")->click();
    return triggered;
}
// Respond only to the dialog/picker explicitly expected by the current step.
class DialogDriver final : public QObject {
  public:
    DialogDriver(QString title, QString input = {}, QString button = {})
        : title_(std::move(title)), input_(std::move(input)), button_(std::move(button)) {
        timer_.setInterval(40);
        connect(&timer_, &QTimer::timeout, this, [this] { respond(); });
        timer_.start();
    }
    bool seen{};

  private:
    void respond() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->windowTitle() != title_)
            return;
        seen = true;
        if (auto* input = qobject_cast<ui::components::QuickInputDialog*>(dialog)) {
            if (input->input()->text() != input_)
                input->input()->setText(input_);
            input->accept(); // Validation keeps the dialog open until it is valid.
        } else if (auto* picker = qobject_cast<ui::QuickOpenDialog*>(dialog)) {
            auto* list = picker->findChild<QListWidget*>("quickOpenResults");
            for (int i = 0; list && i < list->count(); ++i)
                if (list->item(i)->data(Qt::UserRole).toString() == input_) {
                    list->setCurrentRow(i);
                    QTest::keyClick(list, Qt::Key_Return);
                    return;
                }
        } else
            for (auto* button : dialog->findChildren<QPushButton*>())
                if (normalized(button->text()) == button_) {
                    button->click();
                    return;
                }
    }
    QTimer timer_;
    QString title_, input_, button_;
};
} // namespace
class ManualSourceControlSmoke final : public QObject {
    Q_OBJECT
  private slots:
    void realRepositoryThroughWorkbench() {
        QStandardPaths::setTestModeEnabled(true);
        const auto root = qEnvironmentVariable("LITECODE_SCM_MANUAL_REPOSITORY");
        if (root.isEmpty())
            QSKIP("Set LITECODE_SCM_MANUAL_REPOSITORY for this opt-in run.");
        QVERIFY(QDir(root).dirName() == "simple-test-debug");
        const QString branch = "litecode-scm-ui-check";
        const QString fileName = "scm-ui-smoke.txt";
        const auto initialBranch = readGit(root, {"branch", "--show-current"});
        QVERIFY(initialBranch == "main" || initialBranch == branch);
        QVERIFY(readGit(root, {"diff", "--cached", "--name-only"}).isEmpty());
        const auto modified = readGit(root, {"diff", "--name-only"});
        QVERIFY(modified.isEmpty() || (initialBranch == branch && modified == fileName));
        QVERIFY(readGit(root, {"stash", "list"}).isEmpty());
        const auto initialHead = readGit(root, {"rev-parse", "refs/heads/main"});
        const auto untrackedBefore = readGit(root, {"ls-files", "--others", "--exclude-standard"});
        const bool existingBranch = !readGit(root, {"branch", "--list", branch}).isEmpty();
        const auto filePath = QDir(root).filePath(fileName);
        QTemporaryDir settingsDir;
        core::SettingsService settings(settingsDir.filePath("settings.ini"));
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::Theme::apply(*qApp, false);
        ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
        QString error;
        QVERIFY2(window.openWorkspaceFolder(root, &error), qPrintable(error));
        window.resize(1200, 850);
        window.show();
        window.findChild<ui::WorkbenchController*>()->showSidebar(ui::SidebarView::SourceControl);
        auto* panel = window.findChild<ui::SourceControlPanel*>();
        auto* git = panel->findChild<workspace::GitService*>();
        auto* tree = panel->findChild<QTreeWidget*>("gitChanges");
        QSignalSpy errors(git, &workspace::GitService::error);
        QTRY_VERIFY(!git->repository().isEmpty() && !git->busy());
        // A previous interrupted opt-in run may have left only our disposable edit.
        if (!modified.isEmpty()) {
            DialogDriver discard("Discard All Changes", {}, "Discard Tracked Only");
            QVERIFY(menu(*panel, {"Changes", "Discard All Changes"}));
            QTRY_VERIFY(!git->busy() && readGit(root, {"diff", "--name-only"}).isEmpty());
        }
        if (initialBranch != "main") {
            DialogDriver picker("Checkout Branch", "refs/heads/main");
            QVERIFY(menu(*panel, {"Checkout Branch..."}));
            QTRY_COMPARE(git->status().branch.section("...", 0, 0), QString("main"));
            QTRY_VERIFY(!git->busy());
        }
        QVERIFY(!QFile::exists(filePath));
        if (!existingBranch) {
            DialogDriver input("Create Branch", branch);
            QVERIFY(menu(*panel, {"Branch", "Create Branch..."}));
            QVERIFY(input.seen);
        } else {
            DialogDriver picker("Checkout Branch", "refs/heads/" + branch);
            QVERIFY(menu(*panel, {"Checkout Branch..."}));
            QTRY_COMPARE(git->status().branch.section("...", 0, 0), branch);
        }
        QTRY_COMPARE(git->status().branch.section("...", 0, 0), branch);
        QTRY_VERIFY(!git->busy());
        qInfo("PASS UI: create/checkout dedicated test branch");
        const auto write = [&](const QByteArray& data) {
            QFile file(filePath);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        const auto refresh = [&] {
            for (auto* button : panel->findChildren<QToolButton*>())
                if (button->toolTip() == "Refresh") {
                    button->click();
                    return;
                }
        };
        const auto item = [&](bool staged) -> QTreeWidgetItem* {
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
                    auto* child = tree->topLevelItem(i)->child(j);
                    if (child->data(0, Qt::UserRole) == fileName &&
                        child->data(0, Qt::UserRole + 1).toBool() == staged)
                        return child;
                }
            return nullptr;
        };
        const auto transfer = [&](bool staged) {
            auto* target = item(staged);
            if (!target)
                return false;
            tree->scrollToItem(target);
            const auto rect = tree->visualItemRect(target);
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, {},
                              QPoint(rect.right() - 34, rect.center().y()));
            return true;
        };
        QVERIFY(write("LiteCode Source Control UI test\nversion one\n" +
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + '\n'));
        refresh();
        QTRY_VERIFY(item(false) && !git->busy());
        QVERIFY(menu(*panel, {"Changes", "Stage All Changes"}));
        QTRY_VERIFY(item(true) && !git->busy());
        QVERIFY(menu(*panel, {"Changes", "Unstage All Changes"}));
        QTRY_VERIFY(item(false) && !item(true) && !git->busy());
        QVERIFY(readGit(root, {"diff", "--cached", "--name-only"}).isEmpty());
        QVERIFY(transfer(false));
        QTRY_VERIFY(item(true) && !git->busy());
        QVERIFY(transfer(true));
        QTRY_VERIFY(item(false) && !item(true) && !git->busy());
        QVERIFY(transfer(false));
        QTRY_VERIFY(item(true) && !git->busy());
        QCOMPARE(readGit(root, {"diff", "--cached", "--name-only"}), fileName);
        qInfo("PASS UI: stage all, unstage all, individual stage and unstage");
        auto* message = panel->findChild<QPlainTextEdit*>("gitCommitMessage");
        auto* commit = panel->findChild<QPushButton*>("gitCommitButton");
        QVERIFY(!commit->isEnabled());
        message->setPlainText("test: exercise LiteCode Source Control UI");
        QVERIFY(commit->isEnabled());
        const auto previousHead = readGit(root, {"rev-parse", "HEAD"});
        commit->click();
        QTRY_VERIFY(!item(true) && !git->busy());
        QVERIFY(readGit(root, {"rev-parse", "HEAD"}) != previousHead);
        QVERIFY(message->toPlainText().isEmpty());
        QCOMPARE(readGit(root, {"show", "--format=", "--name-only", "HEAD"}), fileName);
        qInfo("PASS UI: commit and clear message");
        {
            DialogDriver confirm("Undo Commit", {}, "Undo Commit");
            QVERIFY(menu(*panel, {"Commit", "Undo Commit"}));
        }
        QTRY_VERIFY(!git->busy() && item(false) && !item(true));
        QCOMPARE(readGit(root, {"rev-parse", "HEAD"}), previousHead);
        QCOMPARE(message->toPlainText(), QString("test: exercise LiteCode Source Control UI"));
        QVERIFY(transfer(false));
        QTRY_VERIFY(!git->busy() && item(true));
        commit->click();
        QTRY_VERIFY(!git->busy() && !item(true));
        const auto first = readGit(root, {"rev-parse", "HEAD"});
        QVERIFY(first != previousHead);
        qInfo("PASS UI: undo unpublished commit restores working changes and commit message; "
              "restage/recommit");
        QVERIFY(write("LiteCode Source Control UI test\nversion two\n"));
        refresh();
        QTRY_VERIFY(item(false) && !git->busy());
        const auto rect = tree->visualItemRect(item(false));
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, rect.center());
        QTRY_VERIFY(window.findChild<QWidget*>("gitDiffEditor"));
        QTRY_VERIFY(!git->busy());
        qInfo("PASS UI: open file diff in editor");
        {
            DialogDriver prompt("Stash", {}, "No Message");
            QVERIFY(menu(*panel, {"Stash", "Stash"}));
            QVERIFY(prompt.seen);
        }
        QTRY_VERIFY(!item(false) && !git->busy());
        QVERIFY(!readGit(root, {"stash", "list"}).isEmpty());
        QVERIFY(menu(*panel, {"Stash", "Pop Latest Stash"}));
        QTRY_VERIFY(item(false) && !git->busy());
        QVERIFY(readGit(root, {"stash", "list"}).isEmpty());
        qInfo("PASS UI: stash tracked changes and pop");
        {
            DialogDriver prompt("Discard All Changes", {}, "Discard Tracked Only");
            QVERIFY(menu(*panel, {"Changes", "Discard All Changes"}));
            QVERIFY(prompt.seen);
        }
        QTRY_VERIFY(!item(false) && !git->busy());
        QCOMPARE(readGit(root, {"ls-files", "--others", "--exclude-standard"}), untrackedBefore);
        qInfo("PASS UI: discard tracked changes without deleting untracked project files");
        QVERIFY(menu(*panel, {"Views", "Graph"}));
        QTRY_VERIFY(window.findChild<QTreeWidget*>("gitHistory")->topLevelItemCount() > 0);
        QTRY_VERIFY(!git->busy());
        qInfo("PASS UI: graph loads real commits");
        QVERIFY(menu(*panel, {"Views", "Changes"}));
        QTRY_VERIFY(!git->busy());
        {
            DialogDriver confirm("Publish Branch", {}, "Publish Branch");
            DialogDriver remote("Publish Branch", "origin");
            QVERIFY(menu(*panel, {"Push"}));
            QVERIFY(QTest::qWaitFor(
                [&] {
                    return !git->busy() && readGit(root, {"rev-parse", "--abbrev-ref",
                                                          "@{upstream}"}) == "origin/" + branch;
                },
                30000));
        }
        QCOMPARE(readGit(root, {"ls-remote", "origin", "refs/heads/" + branch}).section('\t', 0, 0),
                 first);
        qInfo("PASS UI: Push publishes the test branch to GitHub; remote SHA verified");
        QVERIFY(menu(*panel, {"Fetch"}));
        QTRY_VERIFY_WITH_TIMEOUT(!git->busy(), 30000);
        QVERIFY(menu(*panel, {"Pull"}));
        QTRY_VERIFY_WITH_TIMEOUT(!git->busy(), 30000);
        qInfo("PASS UI: fetch and pull with configured upstream");
        QVERIFY(menu(*panel, {"Show Git Output"}));
        ui::OutputPanel* output = nullptr;
        for (auto* widget : window.findChildren<QWidget*>())
            if (auto* candidate = dynamic_cast<ui::OutputPanel*>(widget)) {
                output = candidate;
                break;
            }
        QVERIFY(output);
        auto* tabs = window.findChild<ui::components::WorkbenchTabs*>("bottomTabs");
        QCOMPARE(tabs->currentWidget(), static_cast<QWidget*>(output));
        QCOMPARE(output->selectedChannel(), ui::GitOutputChannel::channelId());
        for (int i = 0; i < tabs->count(); ++i)
            QVERIFY(tabs->tabText(i) != "Git Output");
        auto* extensionsPanel = window.findChild<ui::ExtensionsPanel*>();
        const auto channel =
            extensionsPanel->manager()->outputs().channel(ui::GitOutputChannel::channelId());
        QVERIFY(channel && channel->name == "Git" && channel->text.contains("Git:"));
        qInfo("PASS UI: Git log uses Output > Git, without a separate Git Output tab");
        {
            DialogDriver picker("Checkout Branch", "refs/heads/main");
            QVERIFY(menu(*panel, {"Checkout Branch..."}));
            QTRY_COMPARE(git->status().branch.section("...", 0, 0), QString("main"));
        }
        QTRY_VERIFY(!git->busy());
        QCOMPARE(readGit(root, {"rev-parse", "HEAD"}), initialHead);
        QVERIFY(!QFile::exists(filePath));
        QCOMPARE(readGit(root, {"ls-files", "--others", "--exclude-standard"}), untrackedBefore);
        QVERIFY(readGit(root, {"diff", "--cached", "--name-only"}).isEmpty());
        QCOMPARE(errors.size(), 0);
        qInfo("PASS UI: checkout main restores original HEAD; existing files and index preserved");
    }
};
QTEST_MAIN(ManualSourceControlSmoke)
#include "manual_source_control_smoke.moc"
