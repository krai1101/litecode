#include "extensions/HostWindow.h"
#include "extensions/WorkbenchStatusBar.h"
#include "ui/components/CssColor.h"
#include "ui/components/IconLabelButton.h"
#include "ui/extensions/StatusBarService.h"
#include <QAccessible>
#include <QPromise>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
using namespace litecode::extensions;

class StatusBarTests final : public QObject {
    Q_OBJECT
  private slots:
    void stableStateRoundTripAndRejectInvalidFields() {
        wire::StatusBarState state;
        state.itemId = "entry";
        state.name = "Name";
        state.text = "$(zap) Ready";
        state.alignment = StatusBarAlignment::Right;
        state.priority = 4.5;
        state.tooltip = MarkdownString("**Ready**");
        state.color = QString("#123456");
        state.backgroundColor = ThemeColor("statusBarItem.errorBackground");
        state.command = Command{
            "Ignored title", "fixture.command", QString("Ignored tooltip"), {42, "argument"}};
        state.accessibilityInformation =
            AccessibilityInformation{"Ready for action", QString("button")};
        const auto encoded = wire::encodeStatusBarState(state);
        const auto restored = wire::decodeStatusBarState(encoded);
        QVERIFY(restored);
        QCOMPARE(wire::encodeStatusBarState(*restored), encoded);
        auto invalid = encoded;
        invalid.insert("alignment", 1.5);
        QVERIFY(!wire::decodeStatusBarState(invalid));
        invalid = encoded;
        invalid.insert("backgroundColor", "some.unsupported.background");
        QVERIFY(!wire::decodeStatusBarState(invalid));
        invalid = encoded;
        invalid.insert("priority", "4.5");
        QVERIFY(!wire::decodeStatusBarState(invalid));
    }
    void hostCoalescesUpdatesAndDisposalCannotResurrect() {
        WorkbenchStatusBar backend;
        QVector<QJsonObject> sent;
        QTemporaryDir directory;
        HostWindow host(
            directory.path(),
            [&](const QJsonObject& message) {
                sent.append(message);
                QVERIFY(backend.accept("fixture", "Fixture", message));
            },
            "fixture");
        auto item = host.createStatusBarItem("entry", StatusBarAlignment::Right,
                                             std::numeric_limits<double>::infinity());
        QCOMPARE(item->id(), QString("entry"));
        QCOMPARE(item->alignment(), StatusBarAlignment::Right);
        QCOMPARE(*item->priority(), std::numeric_limits<double>::max());
        item->text("First");
        item->show();
        item->text("Final");
        item->name("Friendly");
        QCOMPARE(sent.size(), 0);
        QTRY_COMPARE(sent.size(), 1);
        QCOMPARE(backend.entries().first().state.text, QString("Final"));
        item->color(StatusBarColor(QString("red")));
        item->backgroundColor(ThemeColor("statusBarItem.errorBackground"));
        QTRY_COMPARE(sent.size(), 2);
        QCOMPARE(std::get<ThemeColor>(*backend.entries().first().state.color).id(),
                 QString("statusBarItem.errorForeground"));
        QCOMPARE(std::get<QString>(*item->color()), QString("red"));
        item->backgroundColor(ThemeColor("unsupported"));
        QVERIFY(!item->backgroundColor());
        item->hide();
        item->show();
        item->dispose();
        item->show();
        item->text("Never visible");
        QTRY_VERIFY(backend.entries().isEmpty());
        auto anonymous = host.createStatusBarItem(StatusBarAlignment::Left,
                                                  std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(anonymous->id(), QString("fixture"));
        QVERIFY(!anonymous->priority());
        anonymous->show();
        QTRY_COMPARE(backend.entries().size(), 1);
        host.shutdown();
        backend.removeOwner("fixture");
        anonymous->show();
        QCoreApplication::processEvents();
        QVERIFY(backend.entries().isEmpty());
    }
    void ownerIsolationPriorityCommandsAndMessageStack() {
        WorkbenchStatusBar backend;
        QSignalSpy commands(&backend, &WorkbenchStatusBar::commandRequested);
        const auto set = [&](const QString& owner, quint64 token, wire::StatusBarState state) {
            return backend.accept(owner, owner,
                                  {{"op", "set"},
                                   {"token", QString::number(token)},
                                   {"state", wire::encodeStatusBarState(state)}});
        };
        wire::StatusBarState low;
        low.itemId = "same";
        low.text = "Low";
        low.priority = -1;
        wire::StatusBarState high = low;
        high.text = "High";
        high.priority = 10;
        high.command = Command{"Title ignored", "fixture.command", {}, {42}};
        QVERIFY(set("one", 1, low));
        QVERIFY(set("two", 1, high));
        QCOMPARE(backend.entries().size(), 2);
        QCOMPARE(backend.entries().first().owner, QString("two"));
        const auto highKey = backend.entries().first().key;
        QVERIFY(backend.invoke(highKey));
        QCOMPARE(commands.size(), 1);
        QCOMPARE(commands.first().first().toString(), QString("fixture.command"));
        QCOMPARE(commands.first().at(1).value<QJsonArray>(), QJsonArray{42});
        QVERIFY(!backend.invoke("missing"));
        QVERIFY(
            backend.accept("one", "One", {{"op", "message"}, {"token", "2"}, {"text", "Equal"}}));
        QVERIFY(
            backend.accept("two", "Two", {{"op", "message"}, {"token", "2"}, {"text", "Equal"}}));
        QCOMPARE(backend.entry("status.extensionMessage")->owner, QString("two"));
        backend.removeOwner("two");
        QCOMPARE(backend.entry("status.extensionMessage")->owner, QString("one"));
        QVERIFY(backend.accept("one", "One", {{"op", "messageRemove"}, {"token", "2"}}));
        QVERIFY(!backend.entry("status.extensionMessage"));
        QCOMPARE(backend.entries().size(), 1);
        QVERIFY(!backend.accept("one", "One", {{"op", "remove"}, {"token", "01"}}));
    }
    void temporaryMessagesDisposeByTimeoutPromiseAndExplicitHandle() {
        WorkbenchStatusBar backend;
        QTemporaryDir directory;
        HostWindow host(
            directory.path(),
            [&](const QJsonObject& message) {
                QVERIFY(backend.accept("fixture", "Fixture", message));
            },
            "fixture");
        auto old = host.setStatusBarMessage("Old");
        auto recent = host.setStatusBarMessage("Recent");
        QTRY_VERIFY(backend.entry("status.extensionMessage"));
        QCOMPARE(backend.entry("status.extensionMessage")->state.text, QString("Recent"));
        recent.dispose();
        QTRY_COMPARE(backend.entry("status.extensionMessage")->state.text, QString("Old"));
        QPromise<int> source;
        source.start();
        auto pending = host.setStatusBarMessage("Working", source.future());
        QTRY_COMPARE(backend.entry("status.extensionMessage")->state.text, QString("Working"));
        source.setException(std::make_exception_ptr(std::runtime_error("Expected")));
        source.finish();
        QTRY_COMPARE(backend.entry("status.extensionMessage")->state.text, QString("Old"));
        QPromise<int> completed;
        completed.start();
        auto completedMessage = host.setStatusBarMessage("Completing", completed.future());
        QTRY_COMPARE(backend.entry("status.extensionMessage")->state.text, QString("Completing"));
        completed.addResult(1);
        completed.finish();
        QTRY_COMPARE(backend.entry("status.extensionMessage")->state.text, QString("Old"));
        QPromise<void> cancelled;
        cancelled.start();
        auto cancelledFuture = cancelled.future();
        auto cancelledMessage = host.setStatusBarMessage("Cancelling", cancelledFuture);
        QTRY_COMPARE(backend.entry("status.extensionMessage")->state.text, QString("Cancelling"));
        cancelledFuture.cancel();
        cancelled.finish();
        QTRY_COMPARE(backend.entry("status.extensionMessage")->state.text, QString("Old"));
        old.dispose();
        QTRY_VERIFY(!backend.entry("status.extensionMessage"));
        auto timed = host.setStatusBarMessage("Timed", 20);
        QTRY_VERIFY(backend.entry("status.extensionMessage"));
        QTRY_VERIFY(!backend.entry("status.extensionMessage"));
        timed.dispose();
        pending.dispose();
    }
    void sharedUiRendersInvokesAndExposesAccessibleRoles() {
        WorkbenchStatusBar backend;
        QStatusBar bar;
        bar.resize(800, 30);
        bar.show();
        litecode::ui::StatusBarService service(backend, bar, &bar);
        QTemporaryDir directory;
        HostWindow host(
            directory.path(),
            [&](const QJsonObject& message) {
                QVERIFY(backend.accept("fixture", "Fixture", message));
            },
            "fixture");
        auto item = host.createStatusBarItem("ui");
        item->text("$(zap) Ready");
        item->tooltip(MarkdownString("**Ready**"));
        item->command(QString("fixture.command"));
        item->accessibilityInformation(AccessibilityInformation{"Ready action", QString("link")});
        item->show();
        QTRY_VERIFY(
            bar.findChild<litecode::ui::components::IconLabelButton*>("extensionStatusItem"));
        auto* button =
            bar.findChild<litecode::ui::components::IconLabelButton*>("extensionStatusItem");
        QCOMPARE(button->accessibleName(), QString("Ready action"));
        QVERIFY(button->toolTip().contains("Ready"));
#if QT_CONFIG(accessibility)
        auto* accessible = QAccessible::queryAccessibleInterface(button);
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::Link);
#endif
        QSignalSpy commands(&backend, &WorkbenchStatusBar::commandRequested);
        button->click();
        QCOMPARE(commands.size(), 1);
        item->dispose();
        QTRY_VERIFY(backend.entries().isEmpty());
        QVERIFY(bar.findChildren<litecode::ui::components::IconLabelButton*>("extensionStatusItem")
                    .isEmpty());
    }
    void cssColorsUseCssAlphaOrderAndRejectInjection() {
        using litecode::ui::components::cssColor;
        const auto alpha = cssColor("#12345680");
        QCOMPARE(alpha.red(), 0x12);
        QCOMPARE(alpha.green(), 0x34);
        QCOMPARE(alpha.blue(), 0x56);
        QCOMPARE(alpha.alpha(), 0x80);
        QCOMPARE(cssColor("#0f08").green(), 255);
        QCOMPARE(cssColor("rgb(255 0 0 / 50%)").red(), 255);
        QCOMPARE(cssColor("hsl(120deg 100% 50%)").green(), 255);
        QVERIFY(!cssColor("red; } QWidget { background: black").isValid());
        QVERIFY(!cssColor("rgb(bad 0 0)").isValid());
    }
};
QTEST_MAIN(StatusBarTests)
#include "tst_extension_status_bar.moc"
