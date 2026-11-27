#include "ui/EditorArea.h"
#include "ui/Theme.h"
#include "ui/components/Controls.h"
#include "ui/components/PanelHeader.h"
#include "ui/components/WorkbenchTabs.h"
#include "ui/explorer/ExplorerFileSystemModel.h"
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>
using namespace litecode::ui::components;
class WorkbenchTabsTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { litecode::ui::Theme::apply(*qApp, false); }
    void inputFrameKeepsOneDevicePixelBorder() {
        InputFrame frame;
        frame.resize(162, 28);
        QLineEdit input(&frame);
        input.setGeometry(5, 4, 100, 20);
        frame.show();
        frame.activateWindow();
        input.setFocus();
        QTRY_VERIFY(input.hasFocus());

        const auto image = frame.grab().toImage();
        const auto theme = litecode::ui::Theme::tokens(false);
        const int x = image.width() / 2;
        QCOMPARE(image.pixelColor(x, 0), theme.searchFocusBorder);
        QCOMPARE(image.pixelColor(x, 1), theme.inputSurface);
        QCOMPARE(image.pixelColor(x, image.height() - 1), theme.searchFocusBorder);
        QCOMPARE(image.pixelColor(x, image.height() - 2), theme.inputSurface);
    }
    void editorCloseTooltipUsesTheSharedSurface() {
        QTemporaryDir folder;
        QFile file(folder.filePath("debug_example.py"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("print(1)\n");
        file.close();
        QMainWindow window;
        window.setObjectName("mainWindow");
        auto* editor = new litecode::ui::EditorArea(&window);
        window.setCentralWidget(editor);
        window.resize(560, 350);
        QVERIFY(editor->openFile(file.fileName()));
        window.show();
        auto* close = editor->findChild<QToolButton*>("editorTabClose");
        QTRY_VERIFY(close && close->isVisible());
        const auto center = close->rect().center();
        QHelpEvent help(QEvent::ToolTip, center, close->mapToGlobal(center));
        QApplication::sendEvent(close, &help);
        auto* tip = window.findChild<QLabel*>("workbenchTooltip");
        QVERIFY(tip && tip->isVisible());
        QCOMPARE(tip->text(), QString("Close debug_example.py"));
        QVERIFY(!tip->wordWrap());
        QVERIFY(tip->windowFlags().testFlag(Qt::NoDropShadowWindowHint));
        QVERIFY(tip->windowFlags().testFlag(Qt::WindowTransparentForInput));
        const auto image = tip->grab().toImage();
        QVERIFY(image.pixelColor(0, 0).alpha() < 255);
        const auto scale = image.devicePixelRatio();
        const int bodyTop = tip->contentsMargins().top() - 4;
        QCOMPARE(image.pixelColor(qRound(2 * scale), qRound((bodyTop + 1) * scale)).alpha(), 255);
        bool ink = false;
        for (int y = 6; y < image.height() - 6; ++y)
            for (int x = 10; x < image.width() - 10; ++x)
                ink |= image.pixelColor(x, y).lightness() < 120;
        QVERIFY(ink);
        if (const auto capture = qEnvironmentVariable("LITECODE_TOOLTIP_CAPTURE");
            !capture.isEmpty())
            QVERIFY(image.save(capture));
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(!tip->isVisible());
    }
    void activityTooltipOpensBesideTheIcon() {
        QMainWindow window;
        auto* toolbar = new QToolBar(&window);
        window.addToolBar(Qt::LeftToolBarArea, toolbar);
        auto* action = toolbar->addAction("Explorer");
        action->setToolTip("Explorer (Ctrl+Shift+E)");
        window.resize(560, 350);
        window.show();
        auto* button = toolbar->widgetForAction(action);
        QTRY_VERIFY(button && button->isVisible());
        const auto point = button->rect().center();
        QHelpEvent help(QEvent::ToolTip, point, button->mapToGlobal(point));
        QApplication::sendEvent(button, &help);
        auto* tip = window.findChild<QLabel*>("workbenchTooltip");
        QVERIFY(tip && tip->isVisible());
        QCOMPARE(tip->text(), action->toolTip());
        const auto buttonRight = button->mapToGlobal(button->rect().topRight()).x();
        QVERIFY(tip->geometry().left() <= buttonRight);
        QVERIFY(tip->geometry().left() >= buttonRight - 2);
    }
    void fileTabUpdatesWhenGitStatusChanges() {
        QTemporaryDir folder;
        const QString name = "DebugExample.java";
        QFile file(folder.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("class DebugExample {}\n");
        file.close();
        litecode::ui::ExplorerFileSystemModel explorer;
        litecode::ui::EditorArea editor;
        editor.setDarkTheme(false);
        editor.setGitDecorationModel(explorer);
        editor.resize(560, 350);
        editor.show();
        QVERIFY(editor.openFile(file.fileName()));
        auto* bar = editor.findChild<QTabBar*>("editorTabBar");
        QVERIFY(bar);
        QTRY_VERIFY(bar->tabRect(0).isValid());
        const int cleanWidth = bar->tabRect(0).width();
        litecode::workspace::GitStatus status;
        status.changes.append({name, {}, '?', '?'});
        const auto repository = QDir(folder.path()).canonicalPath();
        explorer.setGitStatus(repository, status);
        QTRY_VERIFY(bar->tabRect(0).width() > cleanWidth);
        QCOMPARE(bar->tabText(0), name);
        QCOMPARE(bar->tabData(0).toMap().value("badge").toString(), QString("U"));
        QCOMPARE(bar->tabToolTip(0), QDir::toNativeSeparators(QFileInfo(file).canonicalFilePath()));
        const auto green = litecode::ui::Theme::tokens(false).gitUntracked;
        QCOMPARE(bar->tabData(0).toMap().value("color").value<QColor>(), green);
        const auto capture = qEnvironmentVariable("LITECODE_GIT_TAB_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(bar->grab(bar->tabRect(0)).save(capture));
        explorer.setGitStatus(repository, {});
        QTRY_COMPARE(bar->tabRect(0).width(), cleanWidth);
        QVERIFY(bar->tabData(0).toMap().value("badge").toString().isEmpty());
    }
    void editorTabsRetainTheirExistingFlatStyle() {
        QTemporaryDir folder;
        litecode::ui::EditorArea editor;
        for (const auto& name : {"__init__.py", "test_example.py", "run-tests.ps1", "testing.md",
                                 "development.toml"}) {
            const auto path = folder.filePath(name);
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("sample\n");
            file.close();
            QVERIFY(editor.openFile(path));
        }
        editor.resize(1000, 350);
        editor.show();
        QTest::qWait(40);
        auto* bar = editor.findChild<QTabBar*>("editorTabBar");
        QVERIFY(bar);
        if (!qEnvironmentVariable("LITECODE_EDITOR_TABS_RESTORE_CAPTURE").isEmpty())
            QVERIFY(bar->grab().save(qEnvironmentVariable("LITECODE_EDITOR_TABS_RESTORE_CAPTURE")));
        const auto image = bar->grab().toImage();
        const auto selected = bar->tabRect(bar->currentIndex());
        const auto scale = image.devicePixelRatio();
        const auto accent = litecode::ui::Theme::tokens(false).accent;
        bool accentFound = false;
        for (int y = qRound(selected.top() * scale); y < qRound((selected.top() + 3) * scale); ++y)
            accentFound |=
                image.pixelColor(qRound(selected.center().x() * scale), y).rgb() == accent.rgb();
        QVERIFY2(accentFound, "The selected file tab must retain its flat accent top border");
    }
    void panelTabsRecoverSpaceWhenTheToolbarChanges() {
        WorkbenchTabs tabs(TabOverflow::Menu);
        tabs.setObjectName("bottomTabs");
        auto* header = new PanelHeader(&tabs);
        tabs.setCornerWidget(header);
        QPlainTextEdit text;
        auto* outputTools = createTextPanelTools(text, "test", header);
        auto* terminalTools = new QWidget(header);
        auto* toolsLayout = new QHBoxLayout(terminalTools);
        toolsLayout->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < 3; ++i) {
            auto* button = new QToolButton(terminalTools);
            button->setFixedSize(24, 24);
            toolsLayout->addWidget(button);
        }
        for (const auto& name : {"Terminal", "Git Output", "Debug Console", "Output"})
            tabs.addTab(new QLabel(name), name);
        header->setTools(outputTools);
        tabs.resize(420, 250);
        tabs.show();
        QTest::qWait(30);
        header->setTools(terminalTools);
        tabs.resize(1100, 250);
        QTest::qWait(30);
        for (int i = 0; i < tabs.count(); ++i) {
            QVERIFY2(tabs.tabBar()->isTabVisible(i), qPrintable(tabs.tabText(i)));
            QVERIFY2(tabs.tabBar()->rect().contains(tabs.tabBar()->tabRect(i)),
                     qPrintable(tabs.tabText(i)));
            QTest::mouseClick(tabs.tabBar(), Qt::LeftButton, Qt::NoModifier,
                              tabs.tabBar()->tabRect(i).center());
            QCOMPARE(tabs.currentIndex(), i);
        }
        auto* more = tabs.findChild<QToolButton*>("panelTabOverflow");
        QVERIFY(more && !more->isVisible());
        const auto capture = qEnvironmentVariable("LITECODE_PANEL_RESPONSIVE_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(tabs.grab().save(capture + "-wide.png"));
        for (int width : {500, 900, 360, 1100}) {
            tabs.resize(width, 250);
            QTest::qWait(30);
            QVERIFY(tabs.tabBar()->isTabVisible(tabs.currentIndex()));
            QVERIFY(header->geometry().right() < tabs.width());
            for (int i = 0; i < tabs.count(); ++i)
                if (tabs.tabBar()->isTabVisible(i))
                    QVERIFY(tabs.tabBar()->rect().contains(tabs.tabBar()->tabRect(i)));
            if (width == 360 && !capture.isEmpty())
                QVERIFY(tabs.grab().save(capture + "-narrow.png"));
        }
    }
    void fileTabsScrollInPixelsWithoutChangingTheDocument() {
        WorkbenchTabs tabs(TabOverflow::Scroll);
        tabs.setObjectName("editorTabs");
        tabs.tabBar()->setObjectName("editorTabBar");
        tabs.setMovable(true);
        tabs.resize(420, 300);
        for (int i = 0; i < 10; ++i) {
            auto* page = new QLabel(QString::number(i));
            page->setObjectName(QString("page%1").arg(i));
            tabs.addTab(page, QString("document_%1.cpp").arg(i));
        }
        tabs.show();
        tabs.setCurrentIndex(9);
        QTRY_VERIFY(tabs.horizontalScrollBar()->maximum() > 0);
        QTRY_VERIFY(tabs.horizontalScrollBar()->value() > 0);
        tabs.horizontalScrollBar()->setValue(20);
        QWheelEvent wheel(QPointF(100, 10), tabs.tabBar()->mapToGlobal(QPoint(100, 10)),
                          QPoint(-30, 0), {}, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate,
                          false);
        QApplication::sendEvent(tabs.tabBar(), &wheel);
        QCOMPARE(tabs.horizontalScrollBar()->value(), 50);
        QCOMPARE(tabs.currentWidget()->objectName(), "page9");
        tabs.horizontalScrollBar()->setValue(80);
        QCOMPARE(tabs.tabBar()->x(), -80);
        tabs.tabBar()->moveTab(9, 2);
        QCOMPARE(tabs.currentWidget()->objectName(), "page9");
        tabs.removeTab(2);
        QCOMPARE(tabs.count(), 9);
        QVERIFY(tabs.currentWidget());
        if (!qEnvironmentVariable("LITECODE_FILE_TABS_CAPTURE").isEmpty())
            QVERIFY(tabs.grab().save(qEnvironmentVariable("LITECODE_FILE_TABS_CAPTURE")));
    }
    void fixedPanelsUseAMenuAndKeepTheirToolsVisible() {
        WorkbenchTabs tabs(TabOverflow::Menu);
        tabs.setObjectName("bottomTabs");
        tabs.resize(460, 250);
        auto* tools = new QLabel("Filter and panel actions");
        tools->setFixedWidth(270);
        tabs.setCornerWidget(tools);
        for (const auto& name : {"Terminal", "Git Output", "Debug Console", "Output"})
            tabs.addTab(new QLabel(name), name);
        tabs.setCurrentIndex(2);
        tabs.show();
        QTRY_VERIFY(!tabs.tabBar()->isTabVisible(0) || !tabs.tabBar()->isTabVisible(1));
        QVERIFY(tabs.tabBar()->isTabVisible(2));
        QVERIFY(!tabs.tabBar()->usesScrollButtons());
        QVERIFY(!tabs.horizontalScrollBar());
        auto* more = tabs.findChild<QToolButton*>("panelTabOverflow");
        QVERIFY(more);
        QVERIFY(more->isVisible());
        bool selected = false;
        QTimer::singleShot(0, &tabs, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu)
                return;
            for (auto* action : menu->actions())
                if (action->text() == "Output") {
                    action->trigger();
                    selected = true;
                    break;
                }
            menu->close();
        });
        QTest::mouseClick(more, Qt::LeftButton);
        QVERIFY(selected);
        QCOMPARE(tabs.currentIndex(), 3);
        tabs.resize(1100, 250);
        QTRY_VERIFY(tabs.tabBar()->isTabVisible(0) && tabs.tabBar()->isTabVisible(1) &&
                    tabs.tabBar()->isTabVisible(2) && tabs.tabBar()->isTabVisible(3));
        QTRY_VERIFY(!more->isVisible());
        QVERIFY(tools->geometry().right() < tabs.width());
        QCOMPARE(tabs.count(), 4);
    }
};
QTEST_MAIN(WorkbenchTabsTests)
#include "tst_workbench_tabs.moc"
