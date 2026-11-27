#include "extensions/ProgressWire.h"
#include "extensions/WorkbenchMessages.h"
#include "ui/components/Controls.h"
#include "ui/components/IconLabelButton.h"
#include "ui/components/ModalDialogQueue.h"
#include "ui/extensions/FileDialogService.h"
#include "ui/extensions/ProgressNotification.h"
#include "ui/extensions/StatusProgressService.h"
#include "ui/extensions/ViewProgressService.h"
#include "ui/extensions/WindowMessageService.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QJsonArray>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QToolButton>
#include <QtTest>
#include <algorithm>

using namespace litecode::extensions;
using namespace litecode::ui;
namespace {
QJsonObject request(quint64 id, bool modal, QJsonArray items = {}) {
    return {{"requestId", QString::number(id)},    {"severity", 2},
            {"message", "<b>Literal message</b>"}, {"modal", modal},
            {"detail", "Modal-only details"},      {"items", items}};
}
QDialog* dialog(QWidget& owner, quint64 id) {
    for (auto* candidate : owner.findChildren<QDialog*>())
        if (candidate->isVisible() &&
            candidate->property("windowMessageId").toString() == QString::number(id))
            return candidate;
    return nullptr;
}
QPushButton* action(QWidget& view, int index) {
    for (auto* button : view.findChildren<QPushButton*>())
        if (button->property("windowMessageAction").isValid() &&
            button->property("windowMessageAction").toInt() == index)
            return button;
    return nullptr;
}
} // namespace
class ExtensionMessageUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void themeIconLabelsUseBundledFontAndPreserveEscapes() {
        components::IconLabelButton label;
        const auto value =
            QStringLiteral("$(loading~spin) Build $(check) \\$(warning) <b>literal</b>");
        label.setIconLabel(value);
        label.resize(label.sizeHint());
        label.show();
        QPixmap image(label.size());
        label.render(&image); // Exercise glyph mapping/font loading, not HTML.
        QCOMPARE(label.text(), value);
        QVERIFY(label.accessibleName().contains("loading"));
        QVERIFY(label.accessibleName().contains("check"));
        QVERIFY(label.accessibleName().contains("$(warning)"));
        QVERIFY(label.accessibleName().contains("<b>literal</b>"));
        const auto families = QFontDatabase::families();
        QVERIFY(std::any_of(families.cbegin(), families.cend(), [](const QString& family) {
            return family.compare(QStringLiteral("codicon"), Qt::CaseInsensitive) == 0;
        }));
        QCOMPARE(components::IconLabelButton::stripIcons("a $(sync~spin) b"),
                 QStringLiteral("a b"));
        QCOMPARE(components::IconLabelButton::stripIcons("a \\$(sync) b"),
                 QStringLiteral("a \\$(sync) b"));
        QCOMPARE(components::IconLabelButton::stripIcons("$(check)ready"), QStringLiteral("ready"));
        QCOMPARE(components::IconLabelButton::stripIcons("$(invalid_name)"),
                 QStringLiteral("$(invalid_name)"));
    }
    void windowProgressAndDismissedNotificationFallback() {
        WorkbenchMessages messages;
        WorkbenchProgress progress;
        QWidget owner;
        owner.resize(800, 600);
        QStatusBar bar(&owner);
        bar.setGeometry(0, 570, 800, 30);
        WindowMessageService messagesUi(messages, &owner, &owner);
        messagesUi.attachProgress(progress);
        StatusProgressService status(progress, bar, &owner);
        connect(&status, &StatusProgressService::revealRequested, &messagesUi,
                &WindowMessageService::revealProgress);
        connect(&messagesUi, &WindowMessageService::progressVisibilityChanged, &status,
                &StatusProgressService::setNotificationVisible);
        owner.show();
        auto* button = bar.findChild<QToolButton*>(QStringLiteral("extensionProgressStatus"));
        QVERIFY(button);
        ProgressOptions options;
        options.location = ProgressLocation::Window;
        options.title = QStringLiteral("Window task");
        options.cancellable = true; // This location must never expose Cancel.
        QVERIFY(progress.accept("fixture", "Fixture",
                                {{"operation", "start"},
                                 {"id", "1"},
                                 {"options", wire::encodeProgressOptions(options)}}));
        QVERIFY(!button->isVisible());
        QTRY_VERIFY(button->isVisible());
        QCOMPARE(button->text(), QStringLiteral("$(loading~spin) Window task"));
        button->click();
        auto* card = owner.findChild<ProgressNotification*>();
        QVERIFY(card);
        QVERIFY(!card->findChild<QPushButton*>(QStringLiteral("progressCancel")));
        QVERIFY(!button->isVisible());
        // Dismissing is a visibility change, not task cancellation/completion.
        card->dismissRequested();
        QVERIFY(progress.task("fixture", 1));
        QVERIFY(!progress.task("fixture", 1)->cancellationRequested);
        QTRY_VERIFY(button->isVisible());
        QVERIFY(progress.accept("fixture", "Fixture", {{"operation", "end"}, {"id", "1"}}));
        QTRY_VERIFY(!button->isVisible());
        options.title = QStringLiteral("Too fast");
        QVERIFY(progress.accept("fixture", "Fixture",
                                {{"operation", "start"},
                                 {"id", "2"},
                                 {"options", wire::encodeProgressOptions(options)}}));
        QVERIFY(progress.accept("fixture", "Fixture", {{"operation", "end"}, {"id", "2"}}));
        QCoreApplication::processEvents();
        QVERIFY(!button->isVisible());
    }
    void viewProgressKeepsConcurrentOwnersAndDoesNotConsumeClicks() {
        WorkbenchProgress progress;
        QWidget body, activity;
        body.resize(300, 400);
        activity.resize(48, 48);
        ViewProgressService service(progress, &body);
        service.registerView("workbench.scm", body, &activity);
        body.show();
        activity.show();
        auto* bar = body.findChild<QProgressBar*>(QStringLiteral("extensionViewProgress"));
        auto* badge = activity.findChild<QProgressBar*>(QStringLiteral("extensionViewProgress"));
        QVERIFY(bar);
        QVERIFY(badge);
        QVERIFY(bar->testAttribute(Qt::WA_TransparentForMouseEvents));
        QVERIFY(!bar->isVisible());
        ProgressOptions options;
        options.location = ProgressLocation::SourceControl;
        const QJsonObject start{
            {"operation", "start"}, {"id", "1"}, {"options", wire::encodeProgressOptions(options)}};
        QVERIFY(progress.accept("one", "One", start));
        QVERIFY(progress.accept("two", "Two", start));
        QVERIFY(bar->isVisible());
        QVERIFY(badge->isVisible());
        QCOMPARE(bar->maximum(), 0);
        QCOMPARE(bar->width(), body.width());
        progress.removeOwner("one");
        QVERIFY(bar->isVisible());
        body.resize(480, 400);
        QCoreApplication::processEvents();
        QCOMPARE(bar->width(), 480);
        progress.removeOwner("two");
        QVERIFY(!bar->isVisible());
        QVERIFY(!badge->isVisible());
        options.location = ProgressOptions::ViewLocation{QStringLiteral("custom.view")};
        QVERIFY(progress.accept("one", "One",
                                {{"operation", "start"},
                                 {"id", "1"},
                                 {"options", wire::encodeProgressOptions(options)}}));
        QWidget lateView;
        service.registerView("custom.view", lateView);
        lateView.show();
        QVERIFY(lateView.findChild<QProgressBar*>()->isVisible());
        progress.removeOwner("one");
        QVERIFY(!lateView.findChild<QProgressBar*>()->isVisible());
    }
    void progressSharesNotificationSurfaceAndCancelsCooperatively() {
        WorkbenchMessages messages;
        WorkbenchProgress progress;
        QWidget window;
        window.resize(800, 600);
        window.show();
        WindowMessageService service(messages, &window, &window);
        service.attachProgress(progress);
        ProgressOptions options;
        options.location = ProgressLocation::Notification;
        options.title = QStringLiteral("<b>Literal task</b>");
        options.cancellable = true;
        QVERIFY(progress.accept("fixture", "Fixture",
                                {{"operation", "start"},
                                 {"id", "1"},
                                 {"options", wire::encodeProgressOptions(options)}}));
        auto* card = window.findChild<ProgressNotification*>();
        QVERIFY(card);
        QVERIFY(card->isVisible());
        QCOMPARE(
            window.findChildren<QScrollArea*>(QStringLiteral("extensionNotificationCenter")).size(),
            1);
        auto* label = card->findChild<QLabel*>(QStringLiteral("progressMessage"));
        QVERIFY(label);
        QCOMPARE(label->textFormat(), Qt::PlainText);
        QCOMPARE(label->text(), QStringLiteral("<b>Literal task</b>"));
        auto* bar = card->findChild<QProgressBar*>();
        QVERIFY(bar);
        QCOMPARE(bar->maximum(), 0);
        QVERIFY(progress.accept(
            "fixture", "Fixture",
            {{"operation", "report"},
             {"id", "1"},
             {"report", wire::encodeProgressReport({QStringLiteral("Working"), 25.0})}}));
        QCOMPARE(bar->maximum(), 1000);
        QCOMPARE(bar->value(), 250);
        QCOMPARE(label->text(), QStringLiteral("<b>Literal task</b>: Working"));
        QSignalSpy cancellation(&progress, &WorkbenchProgress::cancelRequested);
        auto* cancel = card->findChild<QPushButton*>(QStringLiteral("progressCancel"));
        QVERIFY(cancel);
        cancel->click();
        QCOMPARE(cancellation.size(), 1);
        QVERIFY(progress.task("fixture", 1));
        QVERIFY(!card->isVisible());
        QVERIFY(progress.accept(
            "fixture", "Fixture",
            {{"operation", "report"},
             {"id", "1"},
             {"report", wire::encodeProgressReport({QStringLiteral("Cleaning up"), 5.0})}}));
        QVERIFY(!card->isVisible());
        progress.removeOwner("fixture");
        QVERIFY(!progress.task("fixture", 1));
    }
    void modalQueueOrderAndDestroyedService() {
        QWidget window;
        auto& queue = ModalDialogQueue::forWindow(window);
        QCOMPARE(&queue, &ModalDialogQueue::forWindow(window));
        QObject first, second;
        QStringList shown;
        queue.enqueue(first, "1", [&] { shown.append("first-1"); });
        queue.enqueue(first, "2", [&] { shown.append("first-2"); });
        queue.enqueue(second, "1", [&] { shown.append("second-1"); });
        QTRY_COMPARE(shown, QStringList{"first-1"});
        queue.release(first, "1");
        QTRY_COMPARE(shown.size(), 2);
        QCOMPARE(shown.last(), QString("first-2"));
        queue.release(second, "1"); // Cancel another service's queued task.
        queue.release(first, "2");
        QCoreApplication::processEvents();
        QCOMPARE(shown.size(), 2);
        auto* retired = new QObject;
        queue.enqueue(*retired, "active", [&] { shown.append("retired"); });
        queue.enqueue(second, "next", [&] { shown.append("next"); });
        QTRY_COMPARE(shown.last(), QString("retired"));
        delete retired;
        QTRY_COMPARE(shown.last(), QString("next"));
        queue.release(second, "next");
    }
    void messageAndFileDialogsShareOneQueue() {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QWidget window;
        window.resize(800, 600);
        window.show();
        WorkbenchMessages messages;
        WorkbenchDialogs files;
        WindowMessageService messageService(messages, &window, &window);
        FileDialogService fileService(
            files, &window, [] { return QUrl::fromLocalFile(QDir::homePath()); }, &window);
        int messageReplies = 0, fileReplies = 0;
        const auto reply = [&](std::optional<int>, const QString& error) {
            QVERIFY(error.isEmpty());
            ++messageReplies;
        };
        QVERIFY(messages.accept("fixture", "Fixture", request(1, true), reply));
        QVERIFY(files.accept("fixture",
                             {{"requestId", "1"}, {"save", false}, {"options", QJsonObject{}}},
                             [&](std::optional<QVector<QUrl>> selection, const QString& error) {
                                 QVERIFY(!selection);
                                 QVERIFY(error.isEmpty());
                                 ++fileReplies;
                             }));
        QVERIFY(messages.accept("fixture", "Fixture", request(2, true), reply));
        QTRY_VERIFY(dialog(window, 1));
        QVERIFY(window.findChildren<QFileDialog*>().isEmpty());
        dialog(window, 1)->reject();
        QTRY_VERIFY(window.findChild<QFileDialog*>("extensionFileDialog"));
        auto* picker = window.findChild<QFileDialog*>("extensionFileDialog");
        QVERIFY(picker->isVisible());
        QVERIFY(!dialog(window, 2));
        picker->reject();
        QTRY_VERIFY(dialog(window, 2));
        dialog(window, 2)->reject();
        QCOMPARE(messageReplies, 2);
        QCOMPARE(fileReplies, 1);
    }
    void nonmodalSelectionAndManage() {
        QWidget owner;
        owner.resize(800, 600);
        owner.show();
        WorkbenchMessages messages;
        WindowMessageService service(messages, &owner, &owner);
        QSignalSpy manage(&service, &WindowMessageService::manageExtensionRequested);
        int replies = 0;
        std::optional<int> selected;
        QVERIFY(
            messages.accept("fixture", "Fixture",
                            request(1, false, {QJsonObject{{"title", "First"}, {"close", true}}}),
                            [&](std::optional<int> value, const QString& error) {
                                QVERIFY(error.isEmpty());
                                ++replies;
                                selected = value;
                            }));
        auto* card = owner.findChild<QFrame*>("extensionNotificationFrame");
        QVERIFY(card);
        QVERIFY(owner.findChildren<QDialog*>().isEmpty());
        bool literal = false;
        for (auto* text : card->findChildren<QLabel*>()) {
            QVERIFY(text->text() != "Modal-only details");
            if (text->text() == "<b>Literal message</b>") {
                QCOMPARE(text->textFormat(), Qt::PlainText);
                literal = true;
            }
        }
        QVERIFY(literal);
        auto* menuButton = card->findChild<QToolButton*>("notificationActions");
        QVERIFY(menuButton && menuButton->menu());
        menuButton->menu()->actions().first()->trigger();
        QCOMPARE(manage.size(), 1);
        QCOMPARE(manage.first().first().toString(), QString("fixture"));
        QCOMPARE(replies, 0);
        auto* primary = action(*card, 0);
        QVERIFY(primary);
        primary->click();
        QCOMPARE(replies, 1);
        QCOMPARE(selected.value(), 0);
        QVERIFY(!card->isVisible());
    }
    void modalEscapeAndQueue() {
        QWidget owner;
        owner.resize(800, 600);
        owner.show();
        WorkbenchMessages messages;
        WindowMessageService service(messages, &owner, &owner);
        QVector<std::optional<int>> selections;
        const auto reply = [&](std::optional<int> value, const QString& error) {
            QVERIFY(error.isEmpty());
            selections.append(value);
        };
        QVERIFY(messages.accept("fixture", "Fixture",
                                request(1, true,
                                        {QJsonObject{{"title", "Continue"}, {"close", false}},
                                         QJsonObject{{"title", "Dismiss"}, {"close", true}}}),
                                reply));
        QVERIFY(messages.accept("fixture", "Fixture", request(2, true), reply));
        QTRY_VERIFY(dialog(owner, 1));
        QVERIFY(!dialog(owner, 2));
        auto* first = dialog(owner, 1);
        QCOMPARE(first->windowModality(), Qt::WindowModal);
        bool hasDetail = false;
        for (auto* text : first->findChildren<QLabel*>())
            hasDetail |= text->text() == "Modal-only details";
        QVERIFY(hasDetail);
        QTest::keyClick(first, Qt::Key_Escape);
        QCOMPARE(selections.size(), 1);
        QCOMPARE(selections.first().value(), 1);
        QTRY_VERIFY(dialog(owner, 2));
        auto* second = dialog(owner, 2);
        auto* okay = second->findChild<QPushButton*>("windowMessageCancel");
        QVERIFY(okay);
        QCOMPARE(okay->text(), QString("OK"));
        okay->click();
        QCOMPARE(selections.size(), 2);
        QVERIFY(!selections.last());
    }
    void multipleCloseAffordancesAndOwnerRemoval() {
        QWidget owner;
        owner.resize(800, 600);
        owner.show();
        WorkbenchMessages messages;
        WindowMessageService service(messages, &owner, &owner);
        std::optional<int> selected;
        QVERIFY(
            messages.accept("fixture", "Fixture",
                            request(1, true,
                                    {QJsonObject{{"title", "Close one"}, {"close", true}},
                                     QJsonObject{{"title", "Close two"}, {"close", true}}}),
                            [&](std::optional<int> value, const QString&) { selected = value; }));
        QTRY_VERIFY(dialog(owner, 1));
        dialog(owner, 1)->reject();
        QCOMPARE(selected.value(), 1); // Upstream uses the last close affordance.
        int removed = 0;
        const auto reply = [&](std::optional<int> value, const QString& error) {
            QVERIFY(!value);
            QVERIFY(!error.isEmpty());
            ++removed;
        };
        QVERIFY(messages.accept("fixture", "Fixture", request(2, true), reply));
        QVERIFY(messages.accept("fixture", "Fixture", request(3, true), reply));
        QVERIFY(messages.accept("fixture", "Fixture", request(4, false), reply));
        messages.removeOwner("fixture");
        QCOMPARE(removed, 3);
        QCoreApplication::processEvents();
        QVERIFY(!dialog(owner, 2));
        QVERIFY(!dialog(owner, 3));
        auto* center = owner.findChild<QScrollArea*>("extensionNotificationCenter");
        QVERIFY(center && !center->isVisible());
    }
    void notificationsStayWithinOwner() {
        QWidget owner;
        owner.resize(480, 300);
        owner.show();
        WorkbenchMessages messages;
        WindowMessageService service(messages, &owner, &owner);
        int closed = 0;
        for (int id = 1; id <= 12; ++id)
            QVERIFY(messages.accept("fixture", "Fixture", request(id, false),
                                    [&](std::optional<int>, const QString&) { ++closed; }));
        auto* center = owner.findChild<QScrollArea*>("extensionNotificationCenter");
        QVERIFY(center);
        QVERIFY(owner.rect().contains(center->geometry()));
        QTRY_VERIFY(center->verticalScrollBar()->maximum() > 0);
        QCOMPARE(closed, 0);
        owner.resize(320, 240);
        QCoreApplication::processEvents();
        QVERIFY(owner.rect().contains(center->geometry()));
        messages.removeOwner("fixture");
        QCOMPARE(closed, 12);
        QVERIFY(!center->isVisible());
    }
};
QTEST_MAIN(ExtensionMessageUiTests)
#include "tst_extension_message_ui.moc"
