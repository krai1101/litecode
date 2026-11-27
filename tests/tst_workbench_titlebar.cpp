#include "ui/Theme.h"
#include "ui/WorkbenchTitleBar.h"
#include "ui/components/Controls.h"
#include "ui/components/QuickInputDialog.h"
#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>
using namespace litecode::ui;
namespace {
class CloseGuardWindow final : public QWidget {
  public:
    int attempts{};
    bool allowClose{true};

  protected:
    void closeEvent(QCloseEvent* event) override {
        ++attempts;
        event->setAccepted(allowClose);
    }
};
void clickThroughPopup(QWidget* popup, const QPoint& global) {
    const QPointF local = popup->mapFromGlobal(global);
    QMouseEvent press(QEvent::MouseButtonPress, local, QPointF(global), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(popup, &press);
}
WorkbenchTitleBar* addChrome(CloseGuardWindow& window) {
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* chrome = new WorkbenchTitleBar(new QMenuBar, &window);
    layout->addWidget(chrome);
    layout->addStretch();
    window.resize(800, 400);
    window.show();
    return chrome;
}
} // namespace
class WorkbenchTitleBarTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { Theme::apply(*qApp, false); }
    void closeClickDismissesMenuAndClosesWindow() {
        CloseGuardWindow window;
        auto* chrome = addChrome(window);
        QMenu menu(&window);
        bool activated = false;
        auto* item = menu.addAction("Keep open");
        connect(item, &QAction::triggered, &window, [&] { activated = true; });
        menu.popup(window.mapToGlobal(QPoint(30, 70)));
        QTRY_VERIFY(menu.isVisible());
        auto* close = chrome->findChild<QToolButton*>("windowClose");
        clickThroughPopup(&menu, close->mapToGlobal(close->rect().center()));
        QTRY_COMPARE(window.attempts, 1);
        QVERIFY(!window.isVisible());
        QVERIFY(!menu.isVisible());
        QVERIFY(!activated);
    }
    void closeClickWorksWithDropdownAndHonorsCloseGuard() {
        CloseGuardWindow window;
        window.allowClose = false;
        auto* chrome = addChrome(window);
        components::ComboBox combo(&window);
        combo.addItems({"One", "Two"});
        combo.setGeometry(20, 90, 160, 30);
        combo.show();
        QTest::mouseClick(&combo, Qt::LeftButton);
        QTRY_VERIFY(combo.popupWidget()->isVisible());
        auto* close = chrome->findChild<QToolButton*>("windowClose");
        clickThroughPopup(combo.popupWidget(), close->mapToGlobal(close->rect().center()));
        QTRY_COMPARE(window.attempts, 1);
        QVERIFY(window.isVisible());
        QVERIFY(!combo.popupWidget()->isVisible());
        QCOMPARE(combo.currentIndex(), 0);
    }
    void closeClickCancelsInputPopupBeforeClosingWindow() {
        CloseGuardWindow window;
        auto* chrome = addChrome(window);
        components::QuickInputDialog input("Input", "Value", "Enter value", &window);
        input.open();
        QTRY_VERIFY(input.isVisible());
        auto* close = chrome->findChild<QToolButton*>("windowClose");
        clickThroughPopup(&input, close->mapToGlobal(close->rect().center()));
        QTRY_COMPARE(window.attempts, 1);
        QVERIFY(!input.isVisible());
        QCOMPARE(input.result(), int(QDialog::Rejected));
        QVERIFY(!window.isVisible());
    }
    void ordinaryOutsideClickDoesNotCloseWindow() {
        CloseGuardWindow window;
        addChrome(window);
        QMenu menu(&window);
        menu.addAction("Item");
        menu.popup(window.mapToGlobal(QPoint(30, 70)));
        QTRY_VERIFY(menu.isVisible());
        clickThroughPopup(&menu, window.mapToGlobal(QPoint(500, 250)));
        QTest::qWait(20);
        QCOMPARE(window.attempts, 0);
        QVERIFY(window.isVisible());
        menu.close();
    }
    void overlappingPopupTakesPrecedenceOverCaption() {
        CloseGuardWindow window;
        auto* chrome = addChrome(window);
        auto* close = chrome->findChild<QToolButton*>("windowClose");
        const auto position = close->mapToGlobal(close->rect().center());
        QMenu menu(&window);
        menu.addAction("Popup item");
        menu.popup(position - QPoint(10, 10));
        QTRY_VERIFY(menu.isVisible());
        if (menu.geometry().contains(position)) {
            clickThroughPopup(&menu, position);
            QTest::qWait(20);
            QCOMPARE(window.attempts, 0);
            QVERIFY(window.isVisible());
        }
        menu.close();
    }
};
QTEST_MAIN(WorkbenchTitleBarTests)
#include "tst_workbench_titlebar.moc"
