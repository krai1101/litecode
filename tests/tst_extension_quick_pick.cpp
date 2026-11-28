#include "extensions/HostWindow.h"
#include "extensions/QuickPickWire.h"
#include "extensions/WorkbenchQuickInputCoordinator.h"
#include "ui/QuickOpenDialog.h"
#include "ui/extensions/QuickPickService.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode::extensions;

class QuickPickTests final : public QObject {
    Q_OBJECT
  private slots:
    void codecsKeepStableFieldsAndIgnoreSeparatorProperties() {
        QuickPickOptions options;
        options.title = "Title";
        options.prompt = "Prompt";
        options.canPickMany = true;
        options.matchOnDescription = true;
        options.matchOnDetail = false;
        options.ignoreFocusOut = true;
        const auto encoded = wire::encodeQuickPickOptions(options);
        const auto decoded = wire::decodeQuickPickOptions(encoded);
        QVERIFY(decoded);
        QCOMPARE(wire::encodeQuickPickOptions(*decoded), encoded);
        auto invalid = encoded;
        invalid.insert("canPickMany", "true");
        QVERIFY(!wire::decodeQuickPickOptions(invalid));
        QuickPickItem item;
        item.label = "$(file) Entry";
        item.detail = "Details";
        item.picked = true;
        item.resourceUri = QUrl::fromLocalFile("/workspace/entry.py");
        QuickPickItem separator;
        separator.label = "Group";
        separator.kind = QuickPickItemKind::Separator;
        separator.picked = true;
        separator.detail = "Ignored";
        const auto items = wire::encodeQuickPickItems({separator, item});
        QCOMPARE(items.first().toObject().size(), 2);
        const auto restored = wire::decodeQuickPickItems(items);
        QVERIFY(restored);
        QCOMPARE(restored->at(1).detail, item.detail);
        QCOMPARE(restored->at(1).resourceUri, item.resourceUri);
    }
    void typedItemsPreservePayloadAndDuplicateLabels() {
        struct CustomItem : QuickPickItem {
            int payload{};
        };
        WorkbenchQuickPick backend;
        HostWindow* receiver = nullptr;
        QTemporaryDir directory;
        HostWindow host(directory.path(), [&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture", message, [&](const QJsonObject& reply) {
                QVERIFY(receiver->quickPick().accept(reply));
            }));
        });
        receiver = &host;
        QSignalSpy requests(&backend, &WorkbenchQuickPick::requested);
        QSignalSpy items(&backend, &WorkbenchQuickPick::itemsChanged);
        CustomItem first;
        first.label = "Same";
        first.payload = 11;
        CustomItem second;
        second.label = "Same";
        second.payload = 22;
        QuickPickOptions options;
        QString focused;
        options.onDidSelectItem = [&](const std::variant<QuickPickItem, QString>& item) {
            focused = std::get<QuickPickItem>(item).label;
        };
        auto future = host.showQuickPick(QVector<CustomItem>{first, second}, options);
        QTRY_COMPARE(items.size(), 1);
        const auto request = requests.first().first().value<QuickPickRequest>();
        QVERIFY(!backend.complete("other", request.id, QVector<int>{1}));
        QVERIFY(backend.focus("fixture", request.id, 1));
        QCOMPARE(focused, QString("Same"));
        QVERIFY(backend.complete("fixture", request.id, QVector<int>{1}));
        QTRY_VERIFY(future.isFinished());
        QVERIFY(future.result());
        QCOMPARE(std::get<CustomItem>(*future.result()).payload, 22);
    }
    void futureItemsCancellationAndSourceFailure() {
        WorkbenchQuickPick backend;
        HostWindow* receiver = nullptr;
        QTemporaryDir directory;
        HostWindow host(directory.path(), [&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture", message, [&](const QJsonObject& reply) {
                QVERIFY(receiver->quickPick().accept(reply));
            }));
        });
        receiver = &host;
        QSignalSpy requests(&backend, &WorkbenchQuickPick::requested);
        QSignalSpy items(&backend, &WorkbenchQuickPick::itemsChanged);
        QPromise<QVector<QString>> source;
        source.start();
        CancellationTokenSource cancellation;
        auto future = host.showQuickPick(source.future(), {}, cancellation.token());
        QTRY_COMPARE(requests.size(), 1);
        QCOMPARE(items.size(), 0);
        cancellation.cancel();
        QTRY_VERIFY(future.isFinished());
        QVERIFY(!future.result());
        source.addResult({"Late"});
        source.finish();
        QCoreApplication::processEvents();
        QCOMPARE(items.size(), 0);
        QPromise<QVector<QString>> rejected;
        rejected.start();
        auto failed = host.showQuickPick(rejected.future());
        rejected.setException(std::make_exception_ptr(std::runtime_error("Source failure")));
        rejected.finish();
        QTRY_VERIFY(failed.isFinished());
        QVERIFY_EXCEPTION_THROWN(failed.result(), std::exception);
        auto single = host.showQuickPick(QVector<QString>{"Only"});
        QTRY_COMPARE(items.size(), 1);
        const auto request = requests.last().first().value<QuickPickRequest>();
        QVERIFY(!backend.complete("fixture", request.id, QVector<int>{}));
        QVERIFY(!backend.complete("fixture", request.id, QVector<int>{0, 0}));
        host.shutdown();
        QVERIFY(single.isFinished());
        QVERIFY_EXCEPTION_THROWN(single.result(), std::exception);
    }
    void sharedPickerChecksFiltersAndAcceptsEmptyManySelection() {
        WorkbenchQuickPick backend;
        HostWindow* receiver = nullptr;
        QTemporaryDir directory;
        HostWindow host(directory.path(), [&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture", message, [&](const QJsonObject& reply) {
                QVERIFY(receiver->quickPick().accept(reply));
            }));
        });
        receiver = &host;
        QWidget window;
        window.resize(800, 600);
        window.show();
        litecode::ui::QuickPickService service(backend, &window, &window);
        QuickPickOptions options;
        options.canPickMany = true;
        options.ignoreFocusOut = true;
        options.matchOnDetail = true;
        QuickPickItem separator;
        separator.label = "Group";
        separator.kind = QuickPickItemKind::Separator;
        QuickPickItem first;
        first.label = "First";
        first.detail = "Needle";
        first.picked = true;
        QuickPickItem second;
        second.label = "Always";
        second.alwaysShow = true;
        auto future = host.showQuickPick(QVector<QuickPickItem>{separator, first, second}, options);
        QTRY_VERIFY(window.findChild<litecode::ui::QuickOpenDialog*>());
        auto* dialog = window.findChild<litecode::ui::QuickOpenDialog*>();
        auto* list = dialog->findChild<QListWidget*>();
        QVERIFY(list);
        QTRY_COMPARE(list->count(), 3);
        QCOMPARE(dialog->selectedItemIds(), QStringList{"1"});
        QVERIFY(!(list->item(0)->flags() & Qt::ItemIsSelectable));
        dialog->findChild<QLineEdit*>()->setText("Needle");
        QCOMPARE(list->count(), 3);
        list->setCurrentRow(1);
        QTest::keyClick(dialog->findChild<QLineEdit*>(), Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(dialog->selectedItemIds().isEmpty());
        QTest::keyClick(dialog->findChild<QLineEdit*>(), Qt::Key_Return);
        QTRY_VERIFY(future.isFinished());
        QVERIFY(future.result());
        QVERIFY(std::get<QVector<QuickPickItem>>(*future.result()).isEmpty());
    }
    void pickerAndInputBoxReplaceEachOther() {
        WorkbenchQuickPick picks;
        WorkbenchInputBox boxes;
        WorkbenchQuickInputCoordinator coordinator(boxes, picks);
        QVector<QJsonObject> replies;
        const auto send = [&](const QJsonObject& reply) { replies.append(reply); };
        QVERIFY(boxes.accept("one",
                             {{"op", "show"},
                              {"requestId", "1"},
                              {"options", QJsonObject{}},
                              {"hasValidation", false}},
                             send));
        QVERIFY(picks.accept(
            "two", {{"op", "show"}, {"requestId", "1"}, {"options", QJsonObject{}}}, send));
        QCOMPARE(replies.size(), 1);
        QCOMPARE(replies.first().value("command").toString(), QString("inputBox"));
        QVERIFY(replies.first().value("value").isNull());
        QVERIFY(boxes.accept("one",
                             {{"op", "show"},
                              {"requestId", "2"},
                              {"options", QJsonObject{}},
                              {"hasValidation", false}},
                             send));
        QCOMPARE(replies.size(), 2);
        QCOMPARE(replies.last().value("command").toString(), QString("quickPick"));
        QVERIFY(replies.last().value("selection").isNull());
    }
    void oversizedMessagesRejectRatherThanLeavePendingCalls() {
        QVector<QJsonObject> sent;
        HostQuickPick picker([&](const QJsonObject& message) { sent.append(message); });
        auto session = picker.begin({}, {}, {});
        QTRY_COMPARE(sent.size(), 1);
        QuickPickItem huge;
        huge.label = QString(9 * 1024 * 1024, QLatin1Char('x'));
        session.setItems({huge});
        QTRY_VERIFY(session.result.isFinished());
        QVERIFY_EXCEPTION_THROWN(session.result.result(), std::exception);
        QCOMPARE(sent.last().value("op").toString(), QString("cancel"));
        HostInputBox box([&](const QJsonObject& message) { sent.append(message); });
        InputBoxOptions options;
        options.value = huge.label;
        auto failed = box.show(options, {});
        QTRY_VERIFY(failed.isFinished());
        QVERIFY_EXCEPTION_THROWN(failed.result(), std::exception);
    }
};
QTEST_MAIN(QuickPickTests)
#include "tst_extension_quick_pick.moc"
