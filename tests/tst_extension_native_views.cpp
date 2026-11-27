#include "CodexViewLayout.h"
#include "core/SettingsService.h"
#include "editor/ScintillaEditor.h"
#include "extensions/ExtensionManager.h"
#include "extensions/HostNativeViews.h"
#include "extensions/NativeViewWire.h"
#include "extensions/WorkbenchOutput.h"
#include "ui/DiffEditorWidget.h"
#include "ui/EditorArea.h"
#include "ui/ExtensionsPanel.h"
#include "ui/MainWindow.h"
#include "ui/SidebarDragController.h"
#include "ui/SidebarService.h"
#include "ui/Theme.h"
#include "ui/components/ChoiceSlider.h"
#include "ui/components/CircularProgress.h"
#include "ui/components/Controls.h"
#include "ui/components/IconLabelButton.h"
#include "ui/components/PanelHeader.h"
#include "ui/components/ShimmerLabel.h"
#include "ui/components/WorkbenchTabs.h"
#include "ui/extensions/ExtensionDetailsPage.h"
#include "ui/extensions/NativeCodeBlock.h"
#include "ui/extensions/NativeImageView.h"
#include "ui/extensions/NativeInlineCode.h"
#include "ui/extensions/NativeMarkdownView.h"
#include "ui/extensions/NativeTranscriptView.h"
#include "ui/extensions/NativeViewScrollState.h"
#include "ui/extensions/NativeViewService.h"
#include "ui/extensions/NativeViewWidget.h"
#include "ui/extensions/OutputPanel.h"
#include "ui/extensions/WindowMessageService.h"
#include "ui/extensions/WorkbenchDiffCommand.h"
#include "ui/extensions/WorkbenchDocumentOpenService.h"
#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QEnterEvent>
#include <QFile>
#include <QFontInfo>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextFragment>
#include <QTextList>
#include <QToolBar>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>
using namespace litecode;
using namespace litecode::extensions;
namespace {
class TestViewProvider final : public NativeViewProvider {
  public:
    int resolutions{};
    std::shared_ptr<NativeView> view;
    ProviderResult<void> resolveView(std::shared_ptr<NativeView> resolved,
                                     CancellationToken) override {
        ++resolutions;
        view = std::move(resolved);
        NativeControl button;
        button.id = "button";
        button.kind = NativeControlKind::Button;
        button.text = "Native button";
        view->setContent(button);
        return std::monostate{};
    }
};
} // namespace
class NativeViewTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativePasswordInputMasksAndSurvivesUnrelatedFrames() {
        using namespace extensions;
        NativeControl root;
        root.id = "root";
        NativeControl key;
        key.id = "key";
        key.kind = NativeControlKind::Input;
        key.password = true;
        root.children = {key};
        const auto wire = wire::encodeNativeControl(root);
        const auto decoded = wire::decodeNativeControl(wire);
        QVERIFY(decoded);
        QVERIFY(decoded->children.first().password);
        ui::NativeViewWidget view;
        view.setContent(root, 1);
        view.show();
        auto* input = view.findChild<QLineEdit*>("key");
        QVERIFY(input);
        QCOMPARE(input->echoMode(), QLineEdit::Password);
        input->setFocus();
        QTest::keyClicks(input, "fixture-api-key");
        QVERIFY(!input->displayText().contains("fixture-api-key"));
        root.spacing = 12;
        view.setContent(root, 2);
        QCOMPARE(view.findChild<QLineEdit*>("key"), input);
        QCOMPARE(input->text(), QString("fixture-api-key"));
        view.setText("key", {});
        QVERIFY(input->text().isEmpty());
        key.kind = NativeControlKind::Label;
        QVERIFY(!wire::decodeNativeControl(wire::encodeNativeControl(key)));
    }
    void sidebarDragResizeSnapsClosedAndRestoresWidth() {
        using Place = ui::SidebarService::Location;
        for (const auto place : {Place::Primary, Place::Secondary}) {
            QMainWindow window;
            window.setCentralWidget(new QWidget);
            QToolBar rail(&window);
            window.addToolBar(&rail);
            ui::SidebarService sidebars(window, &rail);
            auto* content = new QWidget;
            sidebars.addContainer("view", "View", content, nullptr, place);
            window.resize(1100, 700);
            window.show();
            sidebars.open("view");
            auto* dock = sidebars.dock(place);
            window.resizeDocks({dock}, {320}, Qt::Horizontal);
            QTest::qWait(40);
            const int initialWidth = dock->width();
            const int minimum = qMax(dock->minimumWidth(), dock->minimumSizeHint().width());
            QVERIFY(sidebars.resizeFromDrag(*dock, minimum - 1, initialWidth));
            QVERIFY(dock->isVisible());
            QVERIFY(sidebars.resizeFromDrag(*dock, minimum / 2 - 1, initialWidth));
            QVERIFY(dock->isHidden());
            const auto collapsed = sidebars.saveState();
            QCOMPARE(sidebars.activeContainer(place), QString("view"));
            sidebars.toggle(place);
            QTRY_VERIFY(dock->isVisible());
            QTRY_VERIFY(qAbs(dock->width() - initialWidth) <= 2);
            sidebars.restoreState(collapsed);
            sidebars.applyRestoredState();
            QVERIFY(dock->isHidden());
            sidebars.toggle(place);
            QTRY_VERIFY(dock->isVisible());
            QTRY_VERIFY(qAbs(dock->width() - initialWidth) <= 2);
        }
    }
    void draggingTheSecondarySidebarSashCollapsesAndReleasesTheMouse() {
        QTemporaryDir storage;
        core::SettingsService settings(storage.filePath("settings.ini"));
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
        window.resize(1200, 750);
        window.show();
        QTest::qWait(80);
        auto* sidebars = window.findChild<ui::SidebarService*>();
        QVERIFY(sidebars);
        sidebars->addContainer("resize-view", "Resize View", new QWidget, nullptr,
                               ui::SidebarService::Location::Secondary);
        sidebars->open("resize-view");
        auto* dock = sidebars->dock(ui::SidebarService::Location::Secondary);
        window.resizeDocks({dock}, {320}, Qt::Horizontal);
        QTest::qWait(40);
        const int initialWidth = dock->width();
        const int minimum = qMax(dock->minimumWidth(), dock->minimumSizeHint().width());
        const auto start = QPoint(dock->geometry().left(), dock->geometry().center().y());
        const auto end = start + QPoint(initialWidth - minimum / 2 + 1, 0);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(window.mapToGlobal(end)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &move);
        QVERIFY(dock->isHidden());
        QCOMPARE(QWidget::mouseGrabber(), &window);
        QMouseEvent reopen(QEvent::MouseMove, QPointF(start), QPointF(window.mapToGlobal(start)),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &reopen);
        QTRY_VERIFY(dock->isVisible());
        QTRY_VERIFY(qAbs(dock->width() - initialWidth) <= 2);
        QCOMPARE(QWidget::mouseGrabber(), &window);
        QApplication::sendEvent(&window, &move);
        QVERIFY(dock->isHidden());
        QCOMPARE(QWidget::mouseGrabber(), &window);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(dock->isHidden());
        QVERIFY(QWidget::mouseGrabber() != &window);
        sidebars->toggle(ui::SidebarService::Location::Secondary);
        QTRY_VERIFY(dock->isVisible());
        QTRY_VERIFY(qAbs(dock->width() - initialWidth) <= 2);
    }
    void changesCardShrinksWithoutWrappingTheHeaderOrLosingFileStats() {
        litecode::codex::CodexTurnView turn;
        turn.key = "compact";
        turn.status = "completed";
        turn.files = {{"src/ui/extensions/NativeViewWidget.cpp", 9, 1},
                      {"tests/tst_extension_native_views.cpp", 39, 0},
                      {"src/ui/components/IconLabelButton.cpp", 12, 0}};
        litecode::codex::CodexViewState state;
        ui::NativeViewWidget view;
        NativeControl host, space;
        host.id = "host";
        host.kind = NativeControlKind::Column;
        space.id = "space";
        space.kind = NativeControlKind::Label;
        space.stretch = 1;
        host.children = {litecode::codex::codexChangesLayout(turn, state, true), space};
        view.setContent(host, 1);
        view.show();
        int headerHeight = -1;
        for (const int width : {420, 280, 200, 140, 420}) {
            view.setFixedSize(width, 400);
            QTest::qWait(40);
            auto* card = view.findChild<QWidget*>("codex.turn.compact.changes");
            auto* heading = view.findChild<QWidget*>("codex.turn.compact.changesHeader");
            auto* title = view.findChild<QLabel*>("codex.turn.compact.changedLabel");
            QVERIFY(!title->wordWrap());
            if (headerHeight < 0)
                headerHeight = heading->height();
            QCOMPARE(heading->height(), headerHeight);
            auto* undo = view.findChild<QAbstractButton*>("codex.undo.compact");
            QVERIFY2(undo->width() >= undo->sizeHint().width(),
                     "Undo must retain its full caption while the heading yields space");
            QVERIFY2(card->width() <= width,
                     qPrintable(QString("Card=%1, available=%2").arg(card->width()).arg(width)));
            auto* count = view.findChild<QLabel*>("codex.turn.compact.fileStats.0.removed");
            QVERIFY(count->isVisible());
            const auto position = count->mapTo(&view, QPoint{});
            QVERIFY2(position.x() >= 0 && position.x() + count->width() <= width,
                     qPrintable(QString("Stats=%1..%2, available=%3")
                                    .arg(position.x())
                                    .arg(position.x() + count->width())
                                    .arg(width)));
        }
    }
    void transcriptCardsUseLightFill() {
        ui::Theme::apply(*qApp, false);
        NativeControl root;
        root.id = "root";
        NativeControl scroll;
        scroll.id = "transcript";
        scroll.kind = NativeControlKind::Scroll;
        scroll.appearance = NativeControlAppearance::Transcript;
        scroll.stretch = 1;
        NativeControl card;
        card.id = "transcriptCard";
        card.appearance = NativeControlAppearance::Surface;
        NativeControl label;
        label.id = "label";
        label.kind = NativeControlKind::Label;
        label.text = "Edited 2 files";
        card.children = {label};
        NativeControl messages;
        messages.id = "messages";
        messages.children = {card};
        scroll.children = {messages};
        root.children = {scroll};
        ui::NativeViewWidget view;
        view.resize(300, 200);
        view.setContent(root, 1);
        view.show();
        auto* widget = view.findChild<QWidget*>(card.id);
        QTRY_VERIFY(widget && widget->isVisible() && widget->width() > 40);
        const auto cardImage = widget->grab().toImage();
        const auto filler = cardImage.pixelColor(cardImage.width() - 20, cardImage.height() / 2);
        QVERIFY2(filler.lightness() > ui::Theme::tokens(false).sidebarSurface.lightness(),
                 qPrintable(filler.name(QColor::HexArgb)));
    }
    void surfaceTextActionsHighlightOnHoverWithoutEnablingUndo() {
        for (const bool enabled : {true, false}) {
            NativeControl root;
            root.id = "surface";
            root.kind = NativeControlKind::Column;
            root.appearance = NativeControlAppearance::Surface;
            root.margin = 20;
            NativeControl undo;
            undo.id = "undo";
            undo.kind = NativeControlKind::Button;
            undo.text = "Undo $(discard)";
            undo.buttonStyle = NativeButtonStyle::Ghost;
            undo.enabled = enabled;
            root.children = {undo};
            ui::NativeViewWidget view;
            view.resize(240, 120);
            view.setContent(root, 1);
            view.show();
            auto* button = view.findChild<QAbstractButton*>("undo");
            button->setAttribute(Qt::WA_UnderMouse, false);
            const auto colour = [&view, button] {
                const auto image = view.grab().toImage();
                const auto point = button->mapTo(&view, QPoint(3, button->height() / 2));
                const auto ratio = image.devicePixelRatio();
                return image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
            };
            const auto idle = colour();
            // Exercise both style states deterministically, including disabled
            // controls, without depending on the desktop's active window.
            button->setAttribute(Qt::WA_UnderMouse, true);
            QEnterEvent enter(QPointF{}, QPointF{}, QPointF{});
            QApplication::sendEvent(button, &enter);
            button->update();
            QVERIFY2(colour() != idle,
                     qPrintable(QString("enabled=%1 hover=%2 iconOnly=%3 fill=%4 idle=%5 actual=%6")
                                    .arg(enabled)
                                    .arg(button->underMouse())
                                    .arg(button->property("nativeIconOnly").toString())
                                    .arg(button->property("nativeFill").toString())
                                    .arg(idle.name())
                                    .arg(colour().name())));
            QCOMPARE(button->isEnabled(), enabled);
        }
    }
    void nativeMenusStayWithinTheWindow_data() {
        QTest::addColumn<bool>("inComposer");
        QTest::addColumn<bool>("nearBottom");
        QTest::newRow("composer") << true << true;
        QTest::newRow("footer-account") << false << true;
        QTest::newRow("header-account") << false << false;
    }
    void nativeMenusStayWithinTheWindow() {
        QFETCH(bool, inComposer);
        QFETCH(bool, nearBottom);
        ui::NativeViewWidget window;
        window.resize(420, 560);
        NativeControl root;
        root.id = "root";
        root.kind = NativeControlKind::Column;
        NativeControl space;
        space.id = "space";
        space.kind = NativeControlKind::Label;
        space.stretch = 1;
        NativeControl composer;
        composer.id = "composer";
        composer.kind = NativeControlKind::Row;
        if (inComposer)
            composer.appearance = NativeControlAppearance::Composer;
        NativeControl add;
        add.id = "add";
        add.kind = NativeControlKind::Button;
        add.text = "$(add)";
        if (inComposer) {
            add.items = {"Files and images", "Goal\nSet a goal to keep pursuing",
                         "Plan mode\nTurn plan mode on"};
        } else {
            auto settings = add;
            settings.id = "settings";
            settings.text = "Codex settings";
            auto logout = settings;
            logout.id = "logout";
            logout.text = "Log out";
            add.children = {settings, logout};
        }
        composer.children = {add};
        root.children = nearBottom ? QVector<NativeControl>{space, composer}
                                   : QVector<NativeControl>{composer, space};
        window.setContent(root, 1);
        window.show();
        auto* button = window.findChild<QAbstractButton*>("add");
        button->click();
        auto* menu = button->findChild<QMenu*>();
        QTRY_VERIFY(menu && menu->isVisible());
        const auto anchor = QRect(button->mapToGlobal(QPoint{}), button->size());
        if (nearBottom)
            QVERIFY(menu->geometry().bottom() < anchor.top());
        else
            QVERIFY(menu->geometry().top() > anchor.bottom());
        QVERIFY(QRect(window.mapToGlobal(QPoint{}), window.size()).contains(menu->geometry()));
        menu->close();
    }
    void descriptiveChoicesWrapWithinNarrowWindows() {
        for (const int width : {360, 820}) {
            QWidget window;
            window.resize(width, 600);
            ui::components::ComboBox combo(&window);
            combo.setGeometry(16, 530, 180, 30);
            combo.setPopupWithinWindow(true);
            combo.setPopupTitle("How should Codex actions be approved?");
            combo.setPopupHelp("Learn more", QUrl("https://developers.openai.com/codex/security/"));
            combo.setDisplayItems(
                {"Ask for approval\nAlways ask to edit external files and use the internet",
                 "Approve for me\nOnly ask for actions detected as potentially unsafe",
                 "Full access\nUnrestricted access to the internet and any file on your computer"});
            combo.setChoicePresentation(
                {":/icons/file.svg", ":/icons/file.svg", ":/icons/file.svg"}, {});
            combo.setCurrentIndex(2);
            window.show();
            QTest::mouseClick(&combo, Qt::LeftButton);
            auto* popup = combo.popupWidget();
            QTRY_VERIFY(popup->isVisible());
            auto* list = popup->findChild<QListView*>();
            QVERIFY(list);
            QTRY_VERIFY(!list->verticalScrollBar()->isVisible());
            QVERIFY(popup->width() <= window.width());
            const auto index = combo.model()->index(2, 0);
            const QFontMetrics metrics(list->font());
            const int description =
                metrics
                    .boundingRect(QRect(0, 0, list->viewport()->width() - 58, 10000),
                                  Qt::TextWordWrap, combo.itemText(2).section('\n', 1))
                    .height();
            QVERIFY2(
                list->visualRect(index).height() >= metrics.height() + description + 6,
                qPrintable(QString("owner=%1 viewport=%2 row=%3 line=%4 description=%5 hint=%6")
                               .arg(width)
                               .arg(list->viewport()->width())
                               .arg(list->visualRect(index).height())
                               .arg(metrics.height())
                               .arg(description)
                               .arg(list->sizeHintForRow(2))));
            if (width == 360)
                QVERIFY(description > metrics.height());
            QTest::keyClick(list, Qt::Key_Escape);
        }
    }
    void textApprovalButtonsDoNotInheritCircularSendSizing() {
        NativeControl root;
        root.id = "approval";
        root.kind = NativeControlKind::Column;
        root.appearance = NativeControlAppearance::Composer;
        NativeControl allow;
        allow.id = "allow";
        allow.kind = NativeControlKind::Button;
        allow.buttonStyle = NativeButtonStyle::Primary;
        allow.text = "Allow once";
        auto send = allow;
        send.id = "send";
        send.text = "$(arrow-up)";
        NativeControl permissions;
        permissions.id = "permissions";
        permissions.kind = NativeControlKind::ComboBox;
        permissions.items = {"Ask for approval\nAsk before requesting more access"};
        permissions.itemIconPaths = {":/icons/file.svg"};
        permissions.currentIndex = 0;
        root.children = {allow, send, permissions};
        ui::NativeViewWidget view;
        view.resize(400, 200);
        view.setContent(root, 1);
        view.show();
        auto* allowButton = view.findChild<QAbstractButton*>("allow");
        auto* sendButton = view.findChild<QAbstractButton*>("send");
        QTRY_VERIFY(allowButton->width() >=
                    allowButton->fontMetrics().horizontalAdvance("Allow once") + 12);
        QVERIFY(sendButton->width() <= 40);
        QVERIFY(view.findChild<QComboBox*>("permissions")->width() <= 32);
    }
    void structuredMenusRoundTripAndRouteLeafActionsWithoutLayoutWidgets() {
        NativeControl root;
        root.id = "menuTrigger";
        root.kind = NativeControlKind::Button;
        root.text = "Options";
        NativeControl header;
        header.id = "identity";
        header.kind = NativeControlKind::Label;
        header.text = "fixture@example.test";
        header.iconPath = "icons/circle-user-round.svg";
        NativeControl submenu = root;
        submenu.id = "copyMenu";
        submenu.text = "Copy";
        NativeControl leaf = root;
        leaf.id = "copyId";
        leaf.text = "Thread ID";
        submenu.children = {leaf};
        root.children = {header, submenu};
        const auto decoded = wire::decodeNativeControl(wire::encodeNativeControl(root));
        QVERIFY(decoded);
        ui::NativeViewWidget view;
        QSignalSpy input(&view, &ui::NativeViewWidget::input);
        view.setContent(*decoded, 1);
        view.show();
        auto* button = view.findChild<QAbstractButton*>("menuTrigger");
        QVERIFY(button);
        QVERIFY(!view.findChild<QAbstractButton*>("copyId"));
        view.setContent(*decoded, 2);
        QCOMPARE(view.findChild<QAbstractButton*>("menuTrigger"), button);
        button->click();
        auto* menu = button->findChild<QMenu*>("menuTrigger.menu");
        QVERIFY(menu && menu->isVisible());
        QVERIFY(!menu->actions().first()->isEnabled());
        auto* child = menu->actions().last()->menu();
        QVERIFY(child);
        view.setText("copyId", "Updated ID");
        QCOMPARE(child->actions().first()->text(), QString("Updated ID"));
        view.setControlEnabled("copyId", false);
        QVERIFY(!child->actions().first()->isEnabled());
        view.setControlEnabled("copyId", true);
        child->actions().first()->trigger();
        QCOMPARE(input.size(), 1);
        const auto event = qvariant_cast<NativeViewEvent>(input.first()[1]);
        QCOMPARE(event.controlId, QString("copyId"));
        QCOMPARE(event.type, QString("clicked"));
        menu->close();
        root.items = {"Ambiguous legacy action"};
        QVERIFY(!wire::decodeNativeControl(wire::encodeNativeControl(root)));
        root.items.clear();
        root.children.first().kind = NativeControlKind::Input;
        QVERIFY(!wire::decodeNativeControl(wire::encodeNativeControl(root)));
    }
    void replacingAnOpenMenuOwnerReleasesPopupInputImmediately_data() {
        QTest::addColumn<bool>("nested");
        QTest::newRow("menu") << false;
        QTest::newRow("submenu") << true;
    }
    void replacingAnOpenMenuOwnerReleasesPopupInputImmediately() {
        QFETCH(bool, nested);
        NativeControl root;
        root.id = "content";
        root.kind = NativeControlKind::Column;
        NativeControl trigger;
        trigger.id = "options";
        trigger.kind = NativeControlKind::Button;
        trigger.text = "Options";
        NativeControl item = trigger;
        item.id = "menuAction";
        item.text = "Copy";
        if (nested) {
            auto leaf = item;
            leaf.id = "nestedAction";
            leaf.text = "Conversation";
            item.children = {leaf};
        }
        trigger.children = {item};
        NativeControl input;
        input.id = "draft";
        input.kind = NativeControlKind::Input;
        root.children = {trigger, input};
        ui::NativeViewWidget view;
        view.setContent(root, 1);
        view.show();
        auto* button = view.findChild<QAbstractButton*>("options");
        button->click();
        auto* menu = button->findChild<QMenu*>();
        QVERIFY(menu && menu->isVisible());
        QCOMPARE(QApplication::activePopupWidget(), menu);
        if (nested)
            menu->actions().first()->menu()->popup(menu->mapToGlobal(QPoint(menu->width(), 0)));
        // Replacing the trigger during a native frame update must close the
        // top-level popup before its old owner is deferred for deletion.
        for (int frame = 2; frame < 22; ++frame) {
            root.children[0].children[0].text = QString("Copy conversation %1").arg(frame);
            view.setContent(root, frame);
            QVERIFY(!QApplication::activePopupWidget());
            QVERIFY(!QWidget::mouseGrabber());
            auto* draft = view.findChild<QLineEdit*>("draft");
            QTest::mouseClick(draft, Qt::LeftButton);
            draft->clear();
            QTest::keyClicks(draft, "still responsive");
            QCOMPARE(draft->text(), QString("still responsive"));
            button = view.findChild<QAbstractButton*>("options");
            button->click();
            if (nested) {
                menu = button->findChild<QMenu*>("options.menu");
                QVERIFY(menu && menu->actions().first()->menu());
                menu->actions().first()->menu()->popup(menu->mapToGlobal(QPoint(menu->width(), 0)));
            }
            QVERIFY(QApplication::activePopupWidget());
        }
        // Host failure/disable clears the whole view, including nested menus.
        view.showError("Extension unavailable");
        QVERIFY(!QApplication::activePopupWidget());
        QVERIFY(!QWidget::mouseGrabber());
    }
    void perEdgeMarginsRoundTripAndRejectMalformedValues() {
        NativeControl root;
        root.id = "content";
        root.kind = NativeControlKind::Column;
        root.contentsMargins = QMargins(16, 0, 16, 0);
        auto encoded = wire::encodeNativeControl(root);
        const auto decoded = wire::decodeNativeControl(encoded);
        QVERIFY(decoded && decoded->contentsMargins);
        QCOMPARE(*decoded->contentsMargins, QMargins(16, 0, 16, 0));
        ui::NativeViewWidget view;
        view.setContent(root, 1);
        QCOMPARE(view.findChild<QWidget*>("content")->layout()->contentsMargins(),
                 QMargins(16, 0, 16, 0));
        encoded.insert("contentsMargins", QJsonArray{16, -1, 16, 0});
        QVERIFY(!wire::decodeNativeControl(encoded));
        encoded.insert("contentsMargins", QJsonArray{16, 0, 16});
        QVERIFY(!wire::decodeNativeControl(encoded));
        encoded.insert("contentsMargins", QJsonArray{16, 0.5, 16, 0});
        QVERIFY(!wire::decodeNativeControl(encoded));
    }
    void narrowHeaderTitlesStayOnOneLineWithoutLosingTheirFullText() {
        ui::NativeViewWidget view;
        view.resize(600, 100);
        NativeControl root, title;
        root.id = "header";
        root.kind = NativeControlKind::Row;
        title.id = "title";
        title.kind = NativeControlKind::Label;
        title.appearance = NativeControlAppearance::Plain;
        title.text =
            "A very long chat title that must truncate instead of wrapping onto another line";
        title.stretch = 1;
        root.children = {title};
        view.setContent(root, 1);
        view.show();
        auto* label = view.findChild<QLabel*>("title");
        QVERIFY(label && !label->wordWrap());
        QTest::qWait(20);
        const auto height = label->height();
        view.resize(260, 100);
        QTest::qWait(20);
        QCOMPARE(label->height(), height);
        QCOMPARE(label->text(), title.text);
        QVERIFY(label->width() < label->fontMetrics().horizontalAdvance(title.text));
    }
    void svgIconsMatchQtBoundsAtTheCurrentDisplayScale() {
        const QIcon icon(QFileInfo(LITECODE_CODEX_PLUGIN).dir().filePath("icons/rotate-ccw.svg"));
        QVERIFY(!icon.isNull());
        ui::components::IconLabelButton button;
        button.setStyleSheet(
            "QToolButton { color: black; background: white; border: 0; padding: 0; }");
        button.setFocusPolicy(Qt::NoFocus);
        button.setIcon(icon);
        button.setIconSize(QSize(14, 14));
        button.setFixedSize(24, 24);
        button.show();
        QTest::qWait(30);
        const auto actual = button.grab().toImage();
        QImage expected(actual.size(), QImage::Format_ARGB32_Premultiplied);
        expected.setDevicePixelRatio(button.devicePixelRatioF());
        expected.fill(Qt::white);
        {
            QPainter painter(&expected);
            icon.paint(&painter, QRect(5, 5, 14, 14));
        }
        const auto ink = [](const QImage& image) {
            QRect bounds;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).lightness() < 160)
                        bounds = bounds.united(QRect(x, y, 1, 1));
            return bounds;
        };
        const auto a = ink(actual), b = ink(expected);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        QVERIFY2(qAbs(a.width() - b.width()) <= 2 && qAbs(a.height() - b.height()) <= 2 &&
                     qAbs(a.x() - b.x()) <= 2 && qAbs(a.y() - b.y()) <= 2,
                 qPrintable(QString("Rendered %1,%2 %3x%4; Qt reference %5,%6 %7x%8 at DPR %9")
                                .arg(a.x())
                                .arg(a.y())
                                .arg(a.width())
                                .arg(a.height())
                                .arg(b.x())
                                .arg(b.y())
                                .arg(b.width())
                                .arg(b.height())
                                .arg(button.devicePixelRatioF())));
    }
    void nativeLabelsKeepTheUiFamilyAndIconHoverStaysTransparent() {
        NativeControl root, label, button;
        root.id = "root";
        root.kind = NativeControlKind::Column;
        label.id = "title";
        label.kind = NativeControlKind::Label;
        label.text = "Edited files";
        label.appearance = NativeControlAppearance::Heading;
        button.id = "history";
        button.kind = NativeControlKind::Button;
        button.text = "$(history)";
        button.buttonStyle = NativeButtonStyle::Ghost;
        root.children = {label, button};
        ui::NativeViewWidget view;
        view.resize(200, 100);
        view.setContent(root, 1);
        view.show();
        QTest::qWait(30);
        auto* title = view.findChild<QLabel*>("title");
        qInfo().noquote() << "Native font requested:" << qApp->font().family()
                          << "resolved UI:" << QFontInfo(qApp->font()).family()
                          << "resolved label:" << QFontInfo(title->font()).family()
                          << "label weight:" << title->font().weight();
        QCOMPARE(QFontInfo(title->font()).family(), QFontInfo(qApp->font()).family());
        auto* action = view.findChild<QAbstractButton*>("history");
        QTest::mouseMove(&view, QPoint(1, 1));
        const auto before = action->grab().toImage();
        QTest::mouseMove(action, action->rect().center());
        QTest::qWait(30);
        const auto after = action->grab().toImage();
        QCOMPARE(after.pixelColor(2, after.height() / 2),
                 before.pixelColor(2, before.height() / 2));
    }
    void proseUsesTheUiFontAndSemiboldOnlyForSourceEmphasis() {
        NativeControl root;
        root.id = "prose";
        root.kind = NativeControlKind::Markdown;
        root.text = "Normal text **emphasis** and regular text.";
        ui::NativeViewWidget view;
        view.setContent(root, 1);
        auto* prose = view.findChild<ui::NativeMarkdownView*>("prose");
        QVERIFY(prose);
        QCOMPARE(prose->document()->defaultFont().family(), qApp->font().family());
        QCOMPARE(prose->document()->defaultFont().weight(), QFont::Normal);
        for (auto block = prose->document()->begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                QCOMPARE(fragment.charFormat().fontWeight(),
                         fragment.text() == "emphasis" ? int(QFont::DemiBold) : int(QFont::Normal));
            }
    }
    void imageThumbnailsKeepColourAndGeometryWhileLoading() {
        QTemporaryDir directory;
        QImage image(120, 80, QImage::Format_RGB32);
        image.fill(Qt::red);
        const auto path = directory.filePath("attachment.png");
        QVERIFY(image.save(path));
        NativeControl root;
        root.id = "image";
        root.kind = NativeControlKind::Image;
        root.iconPath = path;
        root.text = "Attached screenshot";
        root.value = 80;
        auto decoded = wire::decodeNativeControl(wire::encodeNativeControl(root));
        QVERIFY(decoded);
        ui::NativeViewWidget view;
        view.resize(180, 100);
        view.setContent(*decoded, 1);
        view.show();
        auto* thumbnail = view.findChild<ui::NativeImageView*>("image");
        QVERIFY(thumbnail);
        const auto size = thumbnail->size();
        QTRY_VERIFY(thumbnail->imageReady());
        QCOMPARE(thumbnail->size(), size);
        const auto pixels = thumbnail->grab().toImage();
        const auto colour = pixels.pixelColor(pixels.width() / 2, pixels.height() / 2);
        QVERIFY(colour.red() > 240 && colour.green() < 10 && colour.blue() < 10);
    }
    void extensionImageIconsRoundTripRenderAndRejectEscapingPaths() {
        QTemporaryDir package, outside;
        QVERIFY(package.isValid());
        QVERIFY(outside.isValid());
        const auto writeIcon = [](const QString& path) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) &&
                   file.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 "
                              "24\"><path d=\"M3 3h18v18H3z\"/></svg>") > 0;
        };
        QVERIFY(writeIcon(package.filePath("icon.svg")));
        QVERIFY(writeIcon(outside.filePath("icon.svg")));
        NativeControl button;
        button.id = "image";
        button.kind = NativeControlKind::Button;
        button.iconPath = "icon.svg";
        button.iconSize = 12;
        button.text = "Activity";
        auto encoded = wire::encodeNativeControl(button);
        auto decoded = wire::decodeNativeControl(encoded);
        QVERIFY(decoded);
        QCOMPARE(decoded->iconPath, button.iconPath);
        QCOMPARE(decoded->iconSize, 12);
        encoded.insert("iconSize", 1000);
        QVERIFY(!wire::decodeNativeControl(encoded));
        WorkbenchNativeViews backend;
        backend.setContributions(
            {{"view", "View", "container", "View", "activitybar", {}, "owner", package.path()}});
        backend.setVisible("view", true);
        QVERIFY(backend.accept("owner", {{"id", "view"}, {"handle", "1"}, {"action", "register"}}));
        QSignalSpy frames(&backend, &WorkbenchNativeViews::contentChanged);
        const auto content = [&](const NativeControl& control) {
            return backend.accept("owner", {{"id", "view"},
                                            {"handle", "1"},
                                            {"action", "content"},
                                            {"root", wire::encodeNativeControl(control)}});
        };
        QVERIFY(content(button));
        const auto frame = qvariant_cast<NativeControl>(frames.takeFirst()[1]);
        QCOMPARE(frame.iconPath, QFileInfo(package.filePath("icon.svg")).canonicalFilePath());
        ui::NativeViewWidget view;
        view.setContent(frame, 1);
        view.show();
        auto* image = view.findChild<ui::components::IconLabelButton*>("image");
        QVERIFY(image && !image->icon().isNull());
        QCOMPARE(image->iconSize(), QSize(12, 12));
        QVERIFY(!image->grab().isNull());
        button.iconPath = outside.filePath("icon.svg");
        QVERIFY(!content(button));
    }
    void shortTranscriptCanRequestOlderHistoryWithoutFollowingTheBottom() {
        ui::NativeTranscriptView view;
        view.resize(360, 300);
        auto* body = new QWidget;
        body->setFixedHeight(40);
        view.setWidget(body);
        view.setWidgetResizable(true);
        view.show();
        QTRY_COMPARE(view.verticalScrollBar()->maximum(), 0);
        view.restorePosition(0, true);
        QSignalSpy boundaries(&view, &ui::NativeTranscriptView::boundaryReached);
        QWheelEvent wheel(QPointF(10, 10), QPointF(view.viewport()->mapToGlobal(QPoint(10, 10))),
                          QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(view.viewport(), &wheel);
        QCOMPARE(boundaries.size(), 1);
        QVERIFY(boundaries.first()[0].toBool());
        QVERIFY(!view.followingLatest());
        QVERIFY(!ui::NativeViewScrollState::capture(view).followEnd);
    }
    void fencedCodeKeepsListNumbersAndScrollsInsideItsCard() {
        ui::NativeViewWidget widget;
        NativeControl root;
        root.id = "chat";
        root.appearance = NativeControlAppearance::Transcript;
        NativeControl markdown;
        markdown.id = "response";
        markdown.kind = NativeControlKind::Markdown;
        const QString path =
            "C:\\Users\\Example\\Downloads\\LiteCode\\build\\release\\extension-packages\\codex";
        markdown.text = "1. Open **Extensions** (`Ctrl+Shift+X`).\n"
                        "2. Click **Install Extension from Folder**.\n"
                        "3. Select:\n   ```text\n   " +
                        path +
                        "\n   ```\n4. Right-click **Codex** and enable.\n\n"
                        "Codex should appear in the right sidebar.";
        root.children = {markdown};
        widget.resize(460, 420);
        widget.show();
        widget.setContent(root, 1);
        auto* text = widget.findChild<ui::NativeMarkdownView*>("response");
        QVERIFY(text);
        QTRY_VERIFY(text->findChild<QFrame*>("nativeCodeBlock"));
        auto* card =
            dynamic_cast<ui::NativeCodeBlock*>(text->findChild<QFrame*>("nativeCodeBlock"));
        QVERIFY(card);
        QCOMPARE(card->code(), path);
        auto* code = card->findChild<QPlainTextEdit*>();
        QVERIFY(code);
        QTRY_VERIFY(card->isVisible());
        QTRY_VERIFY(code->horizontalScrollBar()->maximum() > 0);
        QCOMPARE(text->horizontalScrollBar()->maximum(), 0);
        bool foundLastStep = false;
        for (auto block = text->document()->begin(); block.isValid(); block = block.next())
            if (block.text().startsWith("Right-click")) {
                QVERIFY(block.textList());
                QCOMPARE(block.textList()->itemText(block), QString("4."));
                foundLastStep = true;
            }
        QVERIFY(foundLastStep);
        QTest::mouseClick(card->findChild<ui::components::IconLabelButton*>(), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), path);
        text->selectAll();
        text->copy();
        QVERIFY(QApplication::clipboard()->text().contains(path));
        QVERIFY(QApplication::clipboard()->text().contains("Right-click Codex"));
        auto selection = text->textCursor();
        selection.clearSelection();
        text->setTextCursor(selection);
        QTRY_VERIFY(widget.updatesEnabled());
        QTest::qWait(60);
        if (!qEnvironmentVariable("LITECODE_NATIVE_CODE_CAPTURE").isEmpty())
            QVERIFY(widget.grab().save(qEnvironmentVariable("LITECODE_NATIVE_CODE_CAPTURE")));
        widget.resize(320, 420);
        QTRY_VERIFY(card->width() < text->viewport()->width());
        QCOMPARE(text->horizontalScrollBar()->maximum(), 0);
    }
    void paneDialogDismissesAndRetiresWithItsView() {
        ui::NativeViewWidget widget;
        NativeControl root;
        root.id = "root";
        NativeControl dialog;
        dialog.id = "dialog";
        dialog.kind = NativeControlKind::Dialog;
        dialog.text = "Rename chat";
        NativeControl input;
        input.id = "name";
        input.kind = NativeControlKind::Input;
        input.text = "Existing name";
        dialog.children = {input};
        root.children = {dialog};
        QVERIFY(wire::decodeNativeControl(wire::encodeNativeControl(root)));
        widget.resize(460, 600);
        widget.show();
        widget.setContent(root, 1);
        QTRY_VERIFY(widget.findChild<QDialog*>("dialog")->isVisible());
        QCOMPARE(widget.findChild<QDialog*>("dialog")->windowModality(), Qt::NonModal);
        QVERIFY(widget.isEnabled());
        QSignalSpy events(&widget, &ui::NativeViewWidget::input);
        QTest::keyClick(widget.findChild<QDialog*>("dialog"), Qt::Key_Escape);
        QCOMPARE(events.size(), 1);
        QCOMPARE(qvariant_cast<NativeViewEvent>(events.first()[1]).type, "dismissed");
        widget.setContent(root, 2);
        QTRY_VERIFY(widget.findChild<QDialog*>("dialog")->isVisible());
        root.children.clear();
        widget.setContent(root, 3);
        QCoreApplication::processEvents();
        QVERIFY(!widget.findChild<QDialog*>("dialog"));
        QCOMPARE(events.size(), 1);
    }
    void buttonMenuIsAnchoredAndPublishesOnlySelections() {
        ui::NativeViewWidget widget;
        extensions::NativeControl menuButton;
        menuButton.id = "test.options";
        menuButton.kind = extensions::NativeControlKind::Button;
        menuButton.text = "$(ellipsis)";
        menuButton.items = {"Rename", "Copy", "Archive"};
        widget.setContent(menuButton, 1);
        widget.resize(320, 120);
        widget.show();
        QSignalSpy events(&widget, &ui::NativeViewWidget::input);
        auto* button = widget.findChild<QAbstractButton*>("test.options");
        QVERIFY(button);
        button->click();
        auto* menu = button->findChild<QMenu*>();
        QVERIFY(menu);
        QVERIFY(menu->isVisible());
        QCOMPARE(menu->actions().size(), 3);
        QCOMPARE(events.size(), 0);
        menu->close();
        QCOMPARE(events.size(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        button->click();
        menu = button->findChild<QMenu*>();
        QVERIFY(menu);
        menu->actions()[1]->trigger();
        QCOMPARE(events.size(), 1);
        const auto event = qvariant_cast<extensions::NativeViewEvent>(events.takeFirst()[1]);
        QCOMPARE(event.controlId, "test.options");
        QCOMPARE(event.type, "selected");
        QCOMPARE(event.text, "Copy");
        QCOMPARE(event.index, 1);
        menu->close();
    }
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        ui::Theme::apply(*qApp, qEnvironmentVariableIsSet("LITECODE_NATIVE_DARK"));
    }
    void realWorkbenchShowsIconOpensViewAndReopensMovedSidebar() {
        QTemporaryDir directory;
        core::SettingsService settings(directory.filePath("settings.ini"));
        settings.saveTheme(qEnvironmentVariableIsSet("LITECODE_NATIVE_DARK") ? "dark" : "light");
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
        auto* panel = window.findChild<ui::ExtensionsPanel*>();
        QVERIFY(panel);
        panel->detailsRequested("missing.extension");
        auto* detailsPage = window.findChild<ui::ExtensionDetailsPage*>();
        QVERIFY(detailsPage);
        QVERIFY(!detailsPage->isWindow());
        auto* editorTabs = window.findChild<QTabBar*>("editorTabBar");
        QVERIFY(editorTabs &&
                !editorTabs->tabIcon(editorTabs->currentIndex()).pixmap(16, 16).isNull());
        panel->detailsRequested("missing.extension");
        QCOMPARE(window.findChildren<ui::ExtensionDetailsPage*>().size(), 1);
        auto& views = panel->manager()->nativeViews();
        views.setContributions(
            {{"view", "Native View", "container", "Native View", "activitybar", {}, "owner", {}}});
        auto* bar = window.findChild<QToolBar*>("activityBar");
        auto* action = window.findChild<QAction*>("extensionContainerAction:owner/container");
        auto* sidebars = window.findChild<ui::SidebarService*>();
        QVERIFY(sidebars);
        auto* dock = sidebars->dock(ui::SidebarService::Location::Primary);
        auto* content = window.findChild<QWidget*>("extensionContainer:owner/container");
        auto* workspaceDock = window.findChild<QDockWidget*>("workspaceDock");
        QVERIFY(bar);
        QVERIFY(action);
        QVERIFY(dock);
        QVERIFY(workspaceDock);
        window.show();
        window.resize(1100, 700);
        window.move(20, 20);
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(bar->actions().contains(action));
        QVERIFY(bar->actions().indexOf(action) <
                bar->actions().indexOf(window.findChild<QAction*>("manageAction")));
        auto* icon = bar->widgetForAction(action);
        QVERIFY(icon);
        QTRY_VERIFY(icon->isVisible());
        QTest::mouseClick(icon, Qt::LeftButton);
        QTRY_VERIFY(dock->isVisible());
        QCOMPARE(dock, workspaceDock);
        QVERIFY(content->isVisible());
        QCOMPARE(sidebars->activeContainer(ui::SidebarService::Location::Primary),
                 "owner/container");
        // The native separator is zero-width. Extension views must use the
        // same transparent edge hit testing as Explorer, on either side.
        const auto resizeSidebar = [&window](QDockWidget* sidebar, int delta) {
            QTest::qWait(30);
            const bool left = window.dockWidgetArea(sidebar) == Qt::LeftDockWidgetArea;
            const QRect geometry = sidebar->geometry();
            const QPoint edge(left ? geometry.right() - 1 : geometry.left() + 1,
                              geometry.center().y());
            const QPoint global = window.mapToGlobal(edge);
            auto* target = window.childAt(edge);
            if (!target) {
                qWarning() << "No widget at sidebar edge" << global;
                return false;
            }
            const auto send = [target](QEvent::Type type, QPoint globalPosition,
                                       Qt::MouseButton button, Qt::MouseButtons buttons) {
                QMouseEvent event(type, target->mapFromGlobal(globalPosition), globalPosition,
                                  button, buttons, Qt::NoModifier);
                QApplication::sendEvent(target, &event);
            };
            // Exercise the shared Qt event path without depending on which
            // desktop application happens to be in front of the test window.
            send(QEvent::MouseMove, global, Qt::NoButton, Qt::NoButton);
            if (target->cursor().shape() != Qt::SizeHorCursor) {
                qWarning() << "Sidebar cursor" << target << target->cursor().shape() << global;
                return false;
            }
            const int before = sidebar->width();
            const QPoint end = global + QPoint(left ? delta : -delta, 0);
            send(QEvent::MouseButtonPress, global, Qt::LeftButton, Qt::LeftButton);
            send(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
            send(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
            QCoreApplication::processEvents();
            // Qt may clamp the requested width to the centre area's minimum.
            // Check direction and bounds, not platform-specific rounding.
            if (sidebar->width() <= before || sidebar->width() > before + delta + 2)
                qWarning() << "Sidebar width" << before << sidebar->width() << delta;
            return sidebar->width() > before && sidebar->width() <= before + delta + 2;
        };
        QVERIFY(resizeSidebar(dock, 50));
        // A lost release (Alt-Tab, a popup, or a replaced child widget) must
        // not turn subsequent mouse movement into an endless sidebar resize.
        const auto exerciseLostRelease = [&](bool deactivate) {
            const auto geometry = dock->geometry();
            const QPoint edge(geometry.right() - 1, geometry.center().y());
            const QPoint global = window.mapToGlobal(edge);
            QMouseEvent press(QEvent::MouseButtonPress, edge, global, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&window, &press);
            if (deactivate) {
                QEvent lostFocus(QEvent::WindowDeactivate);
                QApplication::sendEvent(&window, &lostFocus);
            }
            const int before = dock->width();
            const QPoint moved = edge + QPoint(60, 0);
            QMouseEvent move(QEvent::MouseMove, moved, window.mapToGlobal(moved), Qt::NoButton,
                             deactivate ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&window, &move);
            return dock->width() == before && QWidget::mouseGrabber() != &window;
        };
        QVERIFY(exerciseLostRelease(false));
        QVERIFY(exerciseLostRelease(true));
        window.findChild<QAction*>("extensionsAction")->trigger();
        QTRY_VERIFY(workspaceDock->isVisible());
        QVERIFY(content->isHidden());
        QVERIFY(resizeSidebar(workspaceDock, 40));
        sidebars->move("owner/container", ui::SidebarService::Location::Secondary);
        dock = sidebars->dock(ui::SidebarService::Location::Secondary);
        QCoreApplication::processEvents();
        QVERIFY(resizeSidebar(dock, 50));
        QVERIFY(!bar->actions().contains(action));
        auto* toggle = window.findChild<QToolButton*>("toggleSecondarySidebar");
        QVERIFY(toggle);
        QTest::mouseClick(toggle, Qt::LeftButton);
        QTRY_VERIFY(dock->isHidden());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QTRY_VERIFY(!dock->isHidden());
        auto* chat = window.findChild<QWidget*>("participantChatPanel");
        QVERIFY(chat);
        QTRY_VERIFY(!chat->isVisible());
        QVERIFY(!sidebars->containerVisible("workbench.panel.chat"));
        dock->close();
        QVERIFY(dock->isHidden());
        QVERIFY(!chat->isVisible());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QTRY_VERIFY(!dock->isHidden());
        QVERIFY(!chat->isVisible());
        if (!qEnvironmentVariable("LITECODE_WORKBENCH_VIEW_CAPTURE").isEmpty())
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_WORKBENCH_VIEW_CAPTURE")));
        // Selecting another built-in sidebar must not close the right-side view.
        window.findChild<QAction*>("extensionsAction")->trigger();
        QVERIFY(!dock->isHidden());
        sidebars->move("owner/container", ui::SidebarService::Location::Primary);
        auto* primaryToggle = window.findChild<QToolButton*>("togglePrimarySidebar");
        QVERIFY(primaryToggle);
        QTest::mouseClick(primaryToggle, Qt::LeftButton);
        QVERIFY(workspaceDock->isHidden());
        QTest::mouseClick(primaryToggle, Qt::LeftButton);
        QVERIFY(content->isVisible());
        QCOMPARE(sidebars->activeContainer(ui::SidebarService::Location::Primary),
                 "owner/container");
        QCOMPARE(window.findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly).size(),
                 3);
        views.setContributions({});
    }
    void activityRailCannotBeHiddenThroughQtVisibilityMenu() {
        QTemporaryDir directory;
        core::SettingsService settings(directory.filePath("settings.ini"));
        settings.saveTheme(qEnvironmentVariableIsSet("LITECODE_NATIVE_DARK") ? "dark" : "light");
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        {
            ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
            auto* bar = window.findChild<QToolBar*>("activityBar");
            QVERIFY(bar);
            QCOMPARE(bar->contextMenuPolicy(), Qt::PreventContextMenu);
            QVERIFY(window.createPopupMenu() == nullptr);
            window.show();
            bar->hide(); // Simulate a layout saved by the old checkable Qt menu.
            window.close();
        }
        ui::MainWindow restored(settings, workspace, {search, quick, replace}, false);
        restored.show();
        auto* bar = restored.findChild<QToolBar*>("activityBar");
        QVERIFY(bar);
        QTRY_VERIFY(bar->isVisible());
        QVERIFY(restored.createPopupMenu() == nullptr);
        QVERIFY(!bar->actions().isEmpty());
    }
    void secondaryToggleWorksWithoutViewsAndAfterExtensionRemoval() {
        QTemporaryDir directory;
        core::SettingsService settings(directory.filePath("settings.ini"));
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
        window.show();
        auto* sidebars = window.findChild<ui::SidebarService*>();
        auto* toggle = window.findChild<QToolButton*>("toggleSecondarySidebar");
        QVERIFY(sidebars && toggle);
        auto* dock = sidebars->dock(ui::SidebarService::Location::Secondary);
        auto* empty = dock->findChild<QWidget*>("secondarySidebarEmpty");
        QVERIFY(empty);
        sidebars->setVisible(ui::SidebarService::Location::Secondary, false);
        QTest::mouseClick(toggle, Qt::LeftButton);
        QTRY_VERIFY(dock->isVisible());
        QVERIFY(empty->isVisible());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(dock->isHidden());
        auto& views = window.findChild<ui::ExtensionsPanel*>()->manager()->nativeViews();
        views.setContributions(
            {{"view", "View", "container", "Container", "secondarySidebar", {}, "owner", {}}});
        sidebars->open("owner/container");
        QVERIFY(!empty->isVisible());
        views.setContributions({});
        QVERIFY(empty->isVisible());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(dock->isHidden());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QTRY_VERIFY(dock->isVisible());
        QVERIFY(empty->isVisible());
    }
    void lateEnabledViewRestoresMovedLocation() {
        QByteArray saved;
        {
            WorkbenchNativeViews views;
            QMainWindow window;
            ui::NativeViewService service(views, window, nullptr);
            views.setContributions(
                {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
            auto* sidebars = window.findChild<ui::SidebarService*>();
            QVERIFY(sidebars);
            sidebars->move("owner/container", ui::SidebarService::Location::Secondary);
            saved = sidebars->saveState();
        }
        WorkbenchNativeViews views;
        QMainWindow window;
        QToolBar activity(&window);
        ui::NativeViewService service(views, window, &activity);
        auto* sidebars = window.findChild<ui::SidebarService*>();
        QVERIFY(sidebars);
        sidebars->restoreState(saved);
        views.setContributions(
            {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
        QCOMPARE(sidebars->location("owner/container"), ui::SidebarService::Location::Secondary);
        QCOMPARE(sidebars->activeContainer(ui::SidebarService::Location::Secondary),
                 "owner/container");
        QVERIFY(activity.actions().isEmpty());
    }
    void restoredLayoutCannotReopenUnavailableParticipantChat() {
        QTemporaryDir directory;
        core::SettingsService settings(directory.filePath("settings.ini"));
        settings.saveTheme(qEnvironmentVariableIsSet("LITECODE_NATIVE_DARK") ? "dark" : "light");
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        {
            ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
            auto* sidebars = window.findChild<ui::SidebarService*>();
            QVERIFY(sidebars);
            sidebars->setAvailable("workbench.panel.chat", true);
            window.show();
            sidebars->open("workbench.panel.chat");
            window.close();
        }
        ui::MainWindow restored(settings, workspace, {search, quick, replace}, false);
        auto* chat = restored.findChild<QWidget*>("participantChatPanel");
        QVERIFY(chat);
        restored.show();
        QTRY_VERIFY(!chat->isVisible());
    }
    void viewEntryFollowsDockLocationAndRemainsReopenable() {
        WorkbenchNativeViews views;
        QMainWindow window;
        QToolBar activity(&window);
        window.addToolBar(&activity);
        QMenu menu(&window);
        ui::NativeViewService service(views, window, &activity, nullptr, &menu);
        window.setCentralWidget(new QWidget(&window));
        auto* sidebars = window.findChild<ui::SidebarService*>();
        QVERIFY(sidebars);
        auto* chat = new QWidget;
        sidebars->addContainer("chat", "Chat", chat, nullptr,
                               ui::SidebarService::Location::Secondary);
        views.setContributions(
            {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
        auto* action = window.findChild<QAction*>("extensionContainerAction:owner/container");
        QVERIFY(action);
        QVERIFY(activity.actions().contains(action));
        QVERIFY(menu.actions().contains(action));
        window.resize(1000, 700);
        window.show();
        sidebars->move("owner/container", ui::SidebarService::Location::Secondary);
        QVERIFY(!activity.actions().contains(action));
        service.openView("view");
        auto* dock = sidebars->dock(ui::SidebarService::Location::Secondary);
        QTRY_VERIFY(dock->isVisible());
        auto* tabs = dock->findChild<QTabBar*>("sidebarTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 2);
        QVERIFY(tabs->tabIcon(0).isNull());
        QVERIFY(tabs->tabIcon(1).isNull());
        sidebars->open("chat");
        QVERIFY(chat->isVisible());
        QVERIFY(!sidebars->containerVisible("owner/container"));
        service.openView("view");
        QVERIFY(chat->isHidden());
        dock->hide();
        action->trigger();
        QTRY_VERIFY(dock->isVisible());
        sidebars->move("owner/container", ui::SidebarService::Location::Primary);
        QVERIFY(activity.actions().contains(action));
        views.setContributions({});
        QVERIFY(activity.actions().isEmpty());
        QVERIFY(menu.actions().isEmpty());
    }
    void sharedHostKeepsWidthAndVisibilityAcrossContainerSwitches() {
        QMainWindow window;
        window.setCentralWidget(new QWidget);
        QToolBar bar(&window);
        window.addToolBar(&bar);
        ui::SidebarService sidebars(window, &bar);
        QAction first("Explorer", &window), second("Extension", &window);
        auto* explorer = new QWidget;
        auto* extension = new QWidget;
        sidebars.addContainer("explorer", "Explorer", explorer, &first,
                              ui::SidebarService::Location::Primary);
        sidebars.addContainer("extension", "Extension", extension, &second,
                              ui::SidebarService::Location::Primary);
        QSignalSpy visibility(&sidebars, &ui::SidebarService::containerVisibilityChanged);
        window.resize(1000, 700);
        window.show();
        sidebars.open("explorer");
        auto* primary = sidebars.dock(ui::SidebarService::Location::Primary);
        window.resizeDocks({primary}, {310}, Qt::Horizontal);
        QCoreApplication::processEvents();
        const int width = primary->width();
        sidebars.open("extension");
        QCOMPARE(primary->width(), width);
        QCOMPARE(primary->windowTitle(), "Extension");
        QVERIFY(extension->isVisible());
        QVERIFY(!explorer->isVisible());
        QVERIFY(second.isChecked());
        QVERIFY(!first.isChecked());
        sidebars.toggle(ui::SidebarService::Location::Primary);
        QVERIFY(!sidebars.containerVisible("extension"));
        sidebars.toggle(ui::SidebarService::Location::Primary);
        QVERIFY(sidebars.containerVisible("extension"));
        QCOMPARE(primary->width(), width);
        sidebars.removeContainer("extension");
        QVERIFY(sidebars.containerVisible("explorer"));
        QCOMPARE(primary->windowTitle(), "Explorer");
        QCOMPARE(window.findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly).size(),
                 2);
        QVERIFY(visibility.size() >= 6);
    }
    void legacyExtensionDockPlacementMigratesIntoSharedHost() {
        QByteArray saved;
        {
            QMainWindow legacy;
            legacy.setCentralWidget(new QWidget);
            QDockWidget dock("Extension", &legacy);
            dock.setObjectName("extensionContainer:owner/container");
            dock.setWidget(new QWidget);
            legacy.addDockWidget(Qt::RightDockWidgetArea, &dock);
            legacy.show();
            saved = legacy.saveState();
        }
        WorkbenchNativeViews views;
        QMainWindow window;
        window.setCentralWidget(new QWidget);
        ui::NativeViewService service(views, window, nullptr);
        auto* sidebars = window.findChild<ui::SidebarService*>();
        QVERIFY(sidebars);
        QVERIFY(window.restoreState(saved));
        // The old saved placeholder exists before the extension is enabled.
        sidebars->migrateLegacyDockState();
        views.setContributions(
            {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
        QCOMPARE(sidebars->location("owner/container"), ui::SidebarService::Location::Secondary);
        QVERIFY(sidebars->containerVisible("owner/container"));
        QCOMPARE(window.findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly).size(),
                 2);
    }
    void sidebarDragDropMovesContainersAndRejectsForeignPayloads() {
        QMainWindow window;
        window.setCentralWidget(new QWidget);
        QToolBar bar(&window);
        window.addToolBar(&bar);
        ui::SidebarService sidebars(window, &bar);
        QAction explorer("Explorer", &window), extension("Extension", &window);
        sidebars.addContainer("explorer", "Explorer", new QWidget, &explorer,
                              ui::SidebarService::Location::Primary);
        sidebars.addContainer("extension", "Extension", new QWidget, &extension,
                              ui::SidebarService::Location::Primary);
        window.resize(1000, 700);
        window.show();
        sidebars.open("extension");
        auto* drag = sidebars.findChild<ui::SidebarDragController*>();
        QVERIFY(drag);
        auto mime = drag->createMimeData("extension");
        QVERIFY(mime);
        const auto saved = sidebars.saveState();
        auto* right = sidebars.dock(ui::SidebarService::Location::Secondary);
        QVERIFY(right->isHidden());
        const QPoint edge(window.width() - 10, window.height() / 2);
        QDragEnterEvent enter(edge, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QVERIFY(enter.isAccepted());
        auto* preview = window.findChild<QWidget*>("sidebarDropPreview");
        QVERIFY(preview);
        QVERIFY(preview->isVisible());
        QVERIFY(right->isHidden());
        QCOMPARE(sidebars.saveState(), saved); // Hover does not mutate layout.
        QDragLeaveEvent leave;
        QApplication::sendEvent(&window, &leave);
        QVERIFY(preview->isHidden());
        QCOMPARE(sidebars.saveState(), saved);
        QDragEnterEvent again(edge, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &again);
        QDropEvent drop(edge, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(sidebars.location("extension"), ui::SidebarService::Location::Secondary);
        QVERIFY(sidebars.containerVisible("extension"));
        QVERIFY(sidebars.containerVisible("explorer"));
        QVERIFY(!bar.actions().contains(&extension));
        QVERIFY(preview->isHidden());

        QMainWindow other;
        ui::SidebarService otherSidebars(other, nullptr);
        otherSidebars.addContainer("extension", "Extension", new QWidget, nullptr,
                                   ui::SidebarService::Location::Primary);
        auto* otherDrag = otherSidebars.findChild<ui::SidebarDragController*>();
        auto foreign = otherDrag->createMimeData("extension");
        QDragEnterEvent rejected(edge, Qt::MoveAction, foreign.get(), Qt::LeftButton,
                                 Qt::NoModifier);
        QApplication::sendEvent(&window, &rejected);
        QVERIFY(!rejected.isAccepted());
        QMimeData text;
        text.setText("not a view container");
        QDragEnterEvent unrelated(edge, Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &unrelated);
        QVERIFY(!unrelated.isAccepted());
        const QPoint left(10, window.height() / 2);
        QDragEnterEvent returnEnter(left, Qt::MoveAction, mime.get(), Qt::LeftButton,
                                    Qt::NoModifier);
        QApplication::sendEvent(&window, &returnEnter);
        QDropEvent returnDrop(left, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &returnDrop);
        QVERIFY(returnDrop.isAccepted());
        QCOMPARE(sidebars.location("extension"), ui::SidebarService::Location::Primary);
        QVERIFY(bar.actions().contains(&extension));
        sidebars.removeContainer("extension");
        QVERIFY(!drag->createMimeData("extension"));
        QDragEnterEvent disposed(edge, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &disposed);
        QVERIFY(!disposed.isAccepted());
    }
    void extensionPanelDeclarationsRejectMultiplePanels() {
        auto declaration = QJsonDocument::fromJson(R"({
          "viewsContainers":{"activitybar":[{"id":"panel","title":"Panel"}]},
          "views":{"panel":[{"id":"view","name":"View","type":"native"}]}
        })")
                               .object();
        QVERIFY(wire::validateNativeViewContributions(declaration).isEmpty());
        auto multipleContainers = declaration;
        auto containers = declaration["viewsContainers"].toObject();
        containers["secondarySidebar"] =
            QJsonArray{QJsonObject{{"id", "other"}, {"title", "Other"}}};
        multipleContainers["viewsContainers"] = containers;
        QVERIFY(
            wire::validateNativeViewContributions(multipleContainers).contains("one native panel"));
        auto multipleViews = declaration;
        auto views = declaration["views"].toObject();
        auto array = views["panel"].toArray();
        array.append(QJsonObject{{"id", "other"}, {"name", "Other"}, {"type", "native"}});
        views["panel"] = array;
        multipleViews["views"] = views;
        QVERIFY(wire::validateNativeViewContributions(multipleViews).contains("one native panel"));
    }
    void coreNavigationIsFixedAndPanelToolsShareOneHeader() {
        QTemporaryDir directory;
        core::SettingsService settings(directory.filePath("settings.ini"));
        settings.saveTheme(qEnvironmentVariableIsSet("LITECODE_NATIVE_DARK") ? "dark" : "light");
        workspace::WorkspaceService workspace;
        workspace::WorkspaceSearch search, quick;
        workspace::WorkspaceReplace replace;
        ui::MainWindow window(settings, workspace, {search, quick, replace}, false);
        window.show();
        QTest::qWait(30);
        auto* sidebars = window.findChild<ui::SidebarService*>();
        auto* drag = sidebars->findChild<ui::SidebarDragController*>();
        for (const auto& id :
             {"workbench.view.explorer", "workbench.view.search", "workbench.view.scm",
              "workbench.view.debug", "workbench.view.extensions"}) {
            QVERIFY(!sidebars->canMove(id));
            QVERIFY(!drag->createMimeData(id));
            sidebars->move(id, ui::SidebarService::Location::Secondary);
            QCOMPARE(sidebars->location(id), ui::SidebarService::Location::Primary);
        }
        auto* tabs = window.findChild<litecode::ui::components::WorkbenchTabs*>("bottomTabs");
        auto* header = window.findChild<ui::components::PanelHeader*>();
        auto* output =
            dynamic_cast<ui::OutputPanel*>(window.findChild<QWidget*>("extensionOutputPanel"));
        QVERIFY(tabs && header && output);
        QVERIFY(!tabs->isMovable());
        QVERIFY(!tabs->tabsClosable());
        tabs->setCurrentWidget(output);
        QVERIFY(header->isVisible());
        QVERIFY(header->isAncestorOf(output->controlsWidget()));
        QVERIFY(output->controlsWidget()->isVisible());
        auto& outputBackend = window.findChild<ui::ExtensionsPanel*>()->manager()->outputs();
        QVERIFY(outputBackend.accept("fixture", {{"id", "ui.fixture"},
                                                 {"operation", "create"},
                                                 {"name", "CMake/Build"},
                                                 {"languageId", "log"},
                                                 {"log", false},
                                                 {"level", 3}}));
        output->selectChannel("ui.fixture");
        QVERIFY(outputBackend.accept(
            "fixture", {{"id", "ui.fixture"},
                        {"operation", "append"},
                        {"text", "[proc] Starting build\n[build] Configuring project\n[build] "
                                 "Build completed successfully\n"}}));
        auto* outputText = window.findChild<QPlainTextEdit*>("extensionOutputText");
        QVERIFY(outputText->toPlainText().contains("Build completed successfully"));
        QVERIFY(!window.findChild<QLineEdit*>("outputFilter"));
        QVERIFY(!window.findChild<QToolButton*>("outputScrollLock"));
        auto* debug = window.findChild<QWidget*>("debugConsolePage");
        tabs->setCurrentWidget(debug);
        QVERIFY(header->isVisible());
        auto* filter = window.findChild<QLineEdit*>("debugConsoleFilter");
        QVERIFY(filter && filter->isVisible());
        QVERIFY(!window.findChild<QLineEdit*>("debugConsoleInput")->isEnabled());
        auto* text = window.findChild<QPlainTextEdit*>("debugConsole");
        text->setPlainText("keep this\nexclude this\nkeep excluded");
        filter->setText("keep,!excluded");
        QVERIFY(text->document()->findBlockByNumber(0).isVisible());
        QVERIFY(!text->document()->findBlockByNumber(1).isVisible());
        QVERIFY(!text->document()->findBlockByNumber(2).isVisible());
        filter->clear();
        QVERIFY(text->document()->findBlockByNumber(1).isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("debugConsoleClear"), Qt::LeftButton);
        QVERIFY(text->toPlainText().isEmpty());
        auto* bottomDock = window.findChild<QDockWidget*>("bottomDock");
        const int restoreHeight = bottomDock->height();
        QTest::mouseClick(window.findChild<QAbstractButton*>("panelMaximize"), Qt::LeftButton);
        QVERIFY(window.centralWidget()->isHidden());
        QTest::mouseClick(window.findChild<QAbstractButton*>("panelMaximize"), Qt::LeftButton);
        QVERIFY(!window.centralWidget()->isHidden());
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(bottomDock->height() - restoreHeight) <= 3, 2000);
        QTest::qWait(40);
        if (!qEnvironmentVariable("LITECODE_PANEL_DEBUG_CAPTURE").isEmpty())
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_PANEL_DEBUG_CAPTURE")));
        tabs->setCurrentWidget(output);
        QTest::qWait(40);
        if (!qEnvironmentVariable("LITECODE_PANEL_OUTPUT_CAPTURE").isEmpty())
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_PANEL_OUTPUT_CAPTURE")));
        QTest::mouseClick(window.findChild<QAbstractButton*>("panelClose"), Qt::LeftButton);
        QVERIFY(window.findChild<QDockWidget*>("bottomDock")->isHidden());
    }
    void textUpdatesKeepSelectionAndComposerHeightBounded() {
        ui::NativeViewWidget view;
        NativeControl root;
        root.id = "root";
        NativeControl transcript;
        transcript.id = "transcript";
        transcript.kind = NativeControlKind::Markdown;
        transcript.stretch = 1;
        transcript.text = QString("Paragraph\n\n").repeated(100);
        NativeControl composer;
        composer.id = "composer";
        composer.kind = NativeControlKind::TextEdit;
        composer.text = "draft selection";
        root.children = {transcript, composer};
        view.resize(400, 700);
        view.setContent(root, 1);
        view.show();
        QTest::qWait(20);
        auto* editor = view.findChild<QPlainTextEdit*>("composer");
        QVERIFY(editor);
        QVERIFY(editor->height() < 160);
        auto cursor = editor->textCursor();
        cursor.setPosition(2);
        cursor.setPosition(7, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        auto* browser = view.findChild<QTextBrowser*>("transcript");
        browser->verticalScrollBar()->setValue(20);
        view.setText("transcript", transcript.text + "More");
        QCOMPARE(browser->verticalScrollBar()->value(), 20);
        view.setContent(root, 2);
        editor = view.findChild<QPlainTextEdit*>("composer");
        QCOMPARE(editor->textCursor().selectedText(), QString("aft s"));
        view.setText("composer", QString("line\n").repeated(100));
        QTRY_VERIFY(editor->height() < 260);
    }
    void largeMarkdownResizesAndFrameReplacementKeepInputWorking() {
        ui::NativeViewWidget view;
        NativeControl root, scroll, body, button;
        root.id = "root";
        scroll.id = "scroll";
        scroll.kind = NativeControlKind::Scroll;
        scroll.stretch = 1;
        body.id = "body";
        const QString markdown =
            QString("### Response\n\nA wrapped paragraph with **formatting**.\n\n"
                    "```cpp\nint value = 42;\n```\n\n")
                .repeated(100);
        for (int i = 0; i < 8; ++i) {
            NativeControl text;
            text.id = QString("markdown%1").arg(i);
            text.kind = NativeControlKind::Markdown;
            text.text = markdown;
            body.children.append(text);
        }
        scroll.children = {body};
        button.id = "clickable";
        button.kind = NativeControlKind::Button;
        button.text = "Still responsive";
        root.children = {scroll, button};
        QSignalSpy input(&view, &ui::NativeViewWidget::input);
        view.resize(460, 800);
        view.show();
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer heartbeat;
        int ticks = 0;
        qint64 lastTick = 0, longestGap = 0;
        connect(&heartbeat, &QTimer::timeout, &view, [&] {
            const auto now = elapsed.elapsed();
            longestGap = qMax(longestGap, now - lastTick);
            lastTick = now;
            ++ticks;
        });
        heartbeat.start(5);
        for (quint64 frame = 1; frame <= 8; ++frame) {
            view.setContent(root, frame);
            view.resize(frame % 2 ? 350 : 620, 800);
            QTest::qWait(10);
            QTest::mouseClick(view.findChild<QAbstractButton*>("clickable"), Qt::LeftButton);
        }
        QCOMPARE(input.size(), 8);
        QVERIFY(ticks >= 3);
        qInfo() << "Markdown replacement/resize worst event-loop gap (ms):" << longestGap;
        QVERIFY2(longestGap < 2000, "Large history blocked input for more than two seconds");
    }
    void layoutRangeChangesCannotSwitchReadingBackToFollowingLatest() {
        ui::NativeTranscriptView transcript;
        auto* content = new QWidget;
        content->setFixedSize(200, 1200);
        transcript.setWidget(content);
        transcript.resize(240, 240);
        transcript.show();
        auto* bar = transcript.verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        transcript.restorePosition(bar->maximum() / 2, false);
        content->setFixedHeight(50);
        QTRY_COMPARE(bar->maximum(), 0);
        QVERIFY(!transcript.followingLatest());
        content->setFixedHeight(1200);
        QTRY_VERIFY(bar->maximum() > 0);
        QTest::qWait(40);
        QVERIFY(!transcript.followingLatest());
        QCOMPARE(bar->value(), 0);
    }
    void prependingMessagesKeepsTheOriginalRowAtTheSameViewportPosition() {
        NativeControl root, body;
        root.id = "transcript";
        root.kind = NativeControlKind::Scroll;
        root.appearance = NativeControlAppearance::Transcript;
        body.id = "messages";
        body.kind = NativeControlKind::Column;
        body.margin = 0;
        const auto message = [](int i) {
            NativeControl row;
            row.id = "message." + QString::number(i);
            row.kind = NativeControlKind::Label;
            row.text = QString("Message %1 with wrapped readable content. ").arg(i).repeated(10);
            return row;
        };
        for (int i = 8; i < 16; ++i)
            body.children.append(message(i));
        root.children = {body};
        ui::NativeViewWidget view;
        view.resize(400, 300);
        view.setContent(root, 1);
        view.show();
        auto* transcript = view.findChild<ui::NativeTranscriptView*>("transcript");
        QTRY_VERIFY(transcript->verticalScrollBar()->maximum() > 0);
        QTest::qWait(80); // Let the initial deferred wrapping/restore finish first.
        transcript->restorePosition(0, false);
        const QPointer<QWidget> anchor = view.findChild<QWidget*>("message.8");
        const int offset = anchor->mapTo(transcript->viewport(), QPoint{}).y();
        for (int i = 7; i >= 0; --i)
            body.children.prepend(message(i));
        root.children = {body};
        view.setContent(root, 2);
        QTRY_VERIFY(anchor &&
                    qAbs(anchor->mapTo(transcript->viewport(), QPoint{}).y() - offset) <= 2);
        QTest::qWait(80);
        QCOMPARE(view.findChild<QWidget*>("message.8"), anchor.data());
        QVERIFY(qAbs(anchor->mapTo(transcript->viewport(), QPoint{}).y() - offset) <= 2);
        QVERIFY(!transcript->followingLatest());
        QVERIFY(transcript->verticalScrollBar()->value() <
                transcript->verticalScrollBar()->maximum() - 32);
    }
    void completedWorkCollapseKeepsTheVisibleAnswerAnchored() {
        NativeControl scroll, body, turn, work, response, tail;
        scroll.id = "transcript";
        scroll.kind = NativeControlKind::Scroll;
        scroll.appearance = NativeControlAppearance::Transcript;
        body.id = "messages";
        body.kind = NativeControlKind::Column;
        turn.id = "turn";
        turn.kind = NativeControlKind::Column;
        work.id = "work";
        work.kind = NativeControlKind::Markdown;
        work.text = QString("Progress while working.\n\n").repeated(15);
        response.id = "answer";
        response.kind = NativeControlKind::Markdown;
        response.text = QString("The final answer remains readable.\n\n").repeated(15);
        tail.id = "tail";
        tail.kind = NativeControlKind::Label;
        tail.text = QString("Following content.\n").repeated(8);
        turn.children = {work, response};
        body.children = {turn, tail};
        scroll.children = {body};
        ui::NativeViewWidget view;
        view.resize(400, 280);
        view.setContent(scroll, 1);
        view.show();
        auto* transcript = view.findChild<ui::NativeTranscriptView*>("transcript");
        QTRY_VERIFY(transcript->verticalScrollBar()->maximum() > 0);
        QTest::qWait(60);
        auto* answer = view.findChild<QTextBrowser*>("answer");
        const int position = answer->mapTo(transcript->widget(), QPoint{}).y() + 10;
        transcript->restorePosition(position, false);
        const int offset = answer->mapTo(transcript->viewport(), QPoint{}).y();
        work.visible = false;
        turn.children = {work, response};
        body.children = {turn, tail};
        scroll.children = {body};
        view.setContent(scroll, 2);
        QTRY_VERIFY(qAbs(view.findChild<QTextBrowser*>("answer")
                             ->mapTo(transcript->viewport(), QPoint{})
                             .y() -
                         offset) <= 2);
        QVERIFY(!transcript->followingLatest());
    }
    void transcriptNavigationIsDrivenByUserScrolling() {
        NativeControl root;
        root.id = "transcript";
        root.kind = NativeControlKind::Scroll;
        root.appearance = NativeControlAppearance::Transcript;
        NativeControl body;
        body.id = "body";
        body.kind = NativeControlKind::Column;
        for (int i = 0; i < 14; ++i) {
            NativeControl row;
            row.id = QString("message.%1").arg(i);
            row.kind = NativeControlKind::Label;
            row.text =
                QString("Message %1. A wrapped paragraph of transcript text. ").arg(i).repeated(4);
            if (i == 0) {
                row.kind = NativeControlKind::Markdown;
                row.text = "First response.\n\n```text\nA code block\n```";
            }
            body.children.append(row);
        }
        root.children = {body};
        ui::NativeViewWidget view;
        QVector<NativeViewEvent> events;
        connect(&view, &ui::NativeViewWidget::input, &view,
                [&](quint64, const NativeViewEvent& event) { events.append(event); });
        view.resize(360, 300);
        view.setContent(root, 1);
        view.show();
        auto* scroll = view.findChild<ui::NativeTranscriptView*>("transcript");
        QVERIFY(scroll);
        auto* bar = scroll->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        QTRY_COMPARE(bar->value(), bar->maximum());
        QVERIFY(events.isEmpty());
        // Moving up near the bottom must not ask for newer history.
        bar->setSingleStep(1);
        bar->triggerAction(QAbstractSlider::SliderSingleStepSub);
        QTest::qWait(20);
        QVERIFY(events.isEmpty());
        bar->setSliderDown(true);
        bar->setSliderPosition(bar->minimum());
        bar->setSliderDown(false);
        QTRY_COMPARE(events.size(), 1);
        QCOMPARE(events.first().type, "scrollStart");
        QTRY_VERIFY(scroll->findChild<QPlainTextEdit*>("nativeCodeText"));
        auto* code = scroll->findChild<QPlainTextEdit*>("nativeCodeText");
        QWheelEvent wheel(QPointF(10, 10), QPointF(code->viewport()->mapToGlobal(QPoint(10, 10))),
                          QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(code->viewport(), &wheel);
        QTRY_COMPARE(events.size(), 2);
        QCOMPARE(events.last().type, "scrollStart");
        auto* latest = scroll->findChild<QAbstractButton*>("nativeTranscriptLatest");
        QVERIFY(latest && latest->isVisible());
        QTest::mouseClick(latest, Qt::LeftButton);
        QTRY_COMPARE(events.size(), 3);
        QCOMPARE(events.last().type, "scrollToEnd");
        QCOMPARE(bar->value(), bar->maximum());
        QVERIFY(!latest->isVisible());
        view.setContent(root, 2);
        QTest::qWait(40);
        QCOMPARE(events.size(), 3);
    }
    void disclosureExpansionPreservesClickedHeader() {
        NativeControl root, body, before, toggle, details, after;
        root.id = "transcript";
        root.kind = NativeControlKind::Scroll;
        root.appearance = NativeControlAppearance::Transcript;
        body.id = "body";
        body.kind = NativeControlKind::Column;
        before.id = "before";
        before.kind = NativeControlKind::Label;
        before.text = QString("Earlier message\n").repeated(30);
        toggle.id = "toggle";
        toggle.kind = NativeControlKind::Button;
        toggle.text = "Worked for 1m";
        details.id = "details";
        details.kind = NativeControlKind::Markdown;
        details.text = QString("Progress paragraph.\n\n").repeated(30);
        details.visible = false;
        after.id = "after";
        after.kind = NativeControlKind::Label;
        after.text = "Final answer";
        body.children = {before, toggle, details, after};
        root.children = {body};
        ui::NativeViewWidget view;
        view.resize(400, 300);
        view.setContent(root, 1);
        view.show();
        QTest::qWait(60);
        auto* scroll = view.findChild<ui::NativeTranscriptView*>("transcript");
        auto* button = view.findChild<QAbstractButton*>("toggle");
        const int originalY = button->mapTo(scroll->viewport(), QPoint{}).y();
        connect(&view, &ui::NativeViewWidget::input, &view,
                [&](quint64, const NativeViewEvent& event) {
                    if (event.controlId == "toggle") {
                        root.children[0].children[2].visible = true;
                        view.setContent(root, 2);
                    }
                });
        QTest::mouseClick(button, Qt::LeftButton);
        QTest::qWait(100);
        scroll = view.findChild<ui::NativeTranscriptView*>("transcript");
        button = view.findChild<QAbstractButton*>("toggle");
        const int actualY = button->mapTo(scroll->viewport(), QPoint{}).y();
        QVERIFY2(qAbs(actualY - originalY) <= 2,
                 qPrintable(QString("Header moved from %1 to %2").arg(originalY).arg(actualY)));
        QVERIFY(view.updatesEnabled());
    }
    void clipboardImageBecomesAttachmentWithoutReplacingDraft() {
        NativeControl root;
        root.id = "prompt";
        root.kind = NativeControlKind::TextEdit;
        root.text = "Preserve my draft";
        ui::NativeViewWidget view;
        QVector<NativeViewEvent> events;
        connect(&view, &ui::NativeViewWidget::input, &view,
                [&](quint64, const NativeViewEvent& event) { events.append(event); });
        view.setContent(root, 1);
        view.show();
        QImage image(20, 10, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QApplication::clipboard()->setImage(image);
        auto* editor = view.findChild<QPlainTextEdit*>("prompt");
        editor->paste();
        QCOMPARE(editor->toPlainText(), QString("Preserve my draft"));
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0].type, QString("filesPasted"));
        const auto paths = QJsonDocument::fromJson(events[0].text.toUtf8()).array();
        QCOMPARE(paths.size(), 1);
        QCOMPARE(QImage(paths[0].toString()), image);
        const auto path = paths[0].toString();
        view.setContent(root, 2);
        QVERIFY(QFileInfo::exists(path));
        QApplication::clipboard()->clear();
    }
    void plainDisclosureHasHandCursorWithoutHoverBackground() {
        NativeControl root;
        root.id = "plain";
        root.kind = NativeControlKind::Button;
        root.text = "Worked for 1m $(chevron-right)";
        root.appearance = NativeControlAppearance::Plain;
        root.buttonStyle = NativeButtonStyle::Ghost;
        ui::NativeViewWidget view;
        view.resize(400, 100);
        view.setContent(root, 1);
        view.show();
        QTest::qWait(30);
        auto* button = view.findChild<QAbstractButton*>("plain");
        QVERIFY(button);
        QCOMPARE(
            button->palette().color(QPalette::ButtonText),
            ui::Theme::tokens(qApp->property("litecodeDarkTheme").toBool()).inactiveTabForeground);
        QCOMPARE(button->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseMove(&view, QPoint(1, 1));
        const auto before = button->grab().toImage();
        QTest::mouseMove(button, button->rect().center());
        QTest::qWait(20);
        const auto after = button->grab().toImage();
        const QPoint sample(before.width() - 6, before.height() / 2);
        QCOMPARE(after.pixelColor(sample), before.pixelColor(sample));
    }
    void labeledMenuButtonShowsHoverBackground() {
        NativeControl root;
        root.id = "root";
        NativeControl trigger;
        trigger.id = "menuTrigger";
        trigger.kind = NativeControlKind::Button;
        trigger.buttonStyle = NativeButtonStyle::Ghost;
        trigger.text = "$(device-desktop) Work locally";
        NativeControl action;
        action.id = "menuAction";
        action.kind = NativeControlKind::Button;
        action.text = "Settings";
        trigger.children = {action};
        root.children = {trigger};
        ui::NativeViewWidget view;
        view.resize(200, 100);
        view.setContent(root, 1);
        view.show();
        auto* button = view.findChild<QAbstractButton*>(trigger.id);
        QTRY_VERIFY(button && button->isVisible());
        QTest::mouseMove(&view, view.rect().bottomRight() - QPoint(2, 2));
        const auto before = button->grab().toImage();
        QTest::mouseMove(button, button->rect().center());
        QTRY_VERIFY(button->underMouse());
        const QPoint sample(before.width() - 6, before.height() / 2);
        QTRY_VERIFY(button->grab().toImage().pixelColor(sample) != before.pixelColor(sample));
    }
    void hoverActionsRevealWithoutChangingMessageGeometry() {
        NativeControl root, group, message, actions, copy, time;
        root.id = "root";
        root.kind = NativeControlKind::Column;
        group.id = "group";
        group.kind = NativeControlKind::Column;
        group.appearance = NativeControlAppearance::HoverGroup;
        message.id = "message";
        message.kind = NativeControlKind::Label;
        message.text = "Message content remains stationary.";
        actions.id = "actions";
        actions.kind = NativeControlKind::Row;
        actions.appearance = NativeControlAppearance::HoverActions;
        time.id = "time";
        time.kind = NativeControlKind::Label;
        time.text = "7:09 PM";
        copy.id = "copy";
        copy.kind = NativeControlKind::Button;
        copy.text = "$(copy)";
        actions.children = {time, copy};
        group.children = {message, actions};
        root.children = {group};
        ui::NativeViewWidget view;
        view.resize(400, 200);
        view.setContent(root, 1);
        view.show();
        view.setFocusPolicy(Qt::StrongFocus);
        QApplication::setActiveWindow(&view);
        view.setFocus(Qt::OtherFocusReason);
        QEvent initialLeave(QEvent::Leave);
        QApplication::sendEvent(view.findChild<QWidget*>("group"), &initialLeave);
        auto* row = view.findChild<QWidget*>("actions");
        auto* label = view.findChild<QLabel*>("message");
        auto* effect = static_cast<QGraphicsOpacityEffect*>(row->graphicsEffect());
        QVERIFY(effect);
        QTRY_COMPARE(effect->opacity(), 0.0);
        QVERIFY(row->testAttribute(Qt::WA_TransparentForMouseEvents));
        const auto geometry = label->geometry();
        QEnterEvent enter(QPointF(20, 20), QPointF(20, 20), QPointF(20, 20));
        QApplication::sendEvent(view.findChild<QWidget*>("group"), &enter);
        QTRY_COMPARE(effect->opacity(), 1.0);
        QVERIFY(!row->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(label->geometry(), geometry);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(view.findChild<QWidget*>("group"), &leave);
        QTRY_COMPARE(effect->opacity(), 0.0);
        QCOMPARE(label->geometry(), geometry);
    }
    void startupThemeIconIsLargeAndCentered() {
        NativeControl root, row, logo, spacer;
        root.id = "startup";
        root.kind = NativeControlKind::Column;
        row.id = "row";
        row.kind = NativeControlKind::Row;
        logo.id = "logo";
        logo.kind = NativeControlKind::Button;
        logo.buttonStyle = NativeButtonStyle::Ghost;
        logo.text = "$(openai)";
        logo.value = 56;
        logo.enabled = false;
        spacer.kind = NativeControlKind::Label;
        spacer.stretch = 1;
        spacer.id = "left";
        row.children.append(spacer);
        row.children.append(logo);
        spacer.id = "right";
        row.children.append(spacer);
        spacer.id = "above";
        root.children.append(spacer);
        root.children.append(row);
        spacer.id = "below";
        root.children.append(spacer);
        ui::NativeViewWidget view;
        view.resize(400, 700);
        view.setContent(root, 1);
        view.show();
        QTest::qWait(40);
        auto* icon = view.findChild<ui::components::IconLabelButton*>("logo");
        QVERIFY(icon);
        QCOMPARE(icon->iconSize(), QSize(56, 56));
        QVERIFY(icon->width() >= 64 && icon->height() >= 64);
        const auto center = icon->mapTo(&view, icon->rect().center());
        QVERIFY(qAbs(center.x() - view.rect().center().x()) <= 2);
        QVERIFY(qAbs(center.y() - view.rect().center().y()) <= 2);
        if (!qEnvironmentVariable("LITECODE_STARTUP_CAPTURE").isEmpty())
            QVERIFY(view.grab().save(qEnvironmentVariable("LITECODE_STARTUP_CAPTURE")));
    }
    void inlineCommandsStaySelectableAndUseCompactCodeChips() {
        ui::NativeViewWidget view;
        view.resize(540, 160);
        QFont editorFont("Consolas");
        editorFont.setPixelSize(24);
        view.setCodeFont(editorFont);
        NativeControl root, prose;
        root.id = "root";
        root.kind = NativeControlKind::Column;
        prose.id = "prose";
        prose.kind = NativeControlKind::Markdown;
        prose.text = "All 8 tests pass with `python -m unittest discover -s tests -v`.";
        root.children = {prose};
        view.setContent(root, 1);
        view.show();
        auto* text = view.findChild<ui::NativeMarkdownView*>("prose");
        QVERIFY(text);
        QCOMPARE(text->toPlainText(),
                 "All 8 tests pass with python -m unittest discover -s tests -v.");
        bool found = false;
        for (auto block = text->document()->begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().boolProperty(ui::NativeInlineCodeProperty)) {
                    found = true;
                    QVERIFY(QFontInfo(it.fragment().charFormat().font()).pixelSize() < 24);
                }
        QVERIFY(found);
        QTest::qWait(60);
        const auto image = text->viewport()->grab().toImage();
        const auto background =
            ui::Theme::tokens(qApp->property("litecodeDarkTheme").toBool()).hover;
        int chipPixels = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                chipPixels += image.pixelColor(x, y).rgb() == background.rgb();
        QVERIFY(chipPixels > 20);
        if (!qEnvironmentVariable("LITECODE_INLINE_CODE_CAPTURE").isEmpty())
            QVERIFY(view.grab().save(qEnvironmentVariable("LITECODE_INLINE_CODE_CAPTURE")));
    }
    void contextRingShowsDetailsOnlyOnHover() {
        QWidget window;
        window.resize(360, 200);
        ui::components::CircularProgress meter(&window);
        meter.move(120, 140);
        meter.setValue(44);
        meter.setDetails("Context window:\n44% used (56% left)\n207k / 475k tokens used");
        window.show();
        auto* tip = meter.findChild<QFrame*>("circularProgressTip");
        QVERIFY(tip && !tip->isVisible());
        const auto before = meter.grab().toImage();
        QTest::qWait(60);
        QCOMPARE(meter.grab().toImage(), before); // The meter has no loading animation.
        QCursor::setPos(meter.mapToGlobal(meter.rect().center()));
        QTest::mouseMove(&meter, meter.rect().center());
        QEvent hover(QEvent::ToolTip);
        QApplication::sendEvent(&meter, &hover);
        QVERIFY(tip->isVisible());
        QEvent transientLeave(QEvent::Leave);
        QApplication::sendEvent(&meter, &transientLeave);
        QVERIFY(tip->isVisible());
        if (!qEnvironmentVariable("LITECODE_CONTEXT_TIP_CAPTURE").isEmpty())
            QVERIFY(tip->grab().save(qEnvironmentVariable("LITECODE_CONTEXT_TIP_CAPTURE")));
        QVERIFY(meter.accessibleDescription().contains("207k / 475k"));
        QCursor::setPos(window.mapToGlobal(QPoint(5, 5)));
        QTest::mouseMove(&window, QPoint(5, 5));
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&meter, &leave);
        QTRY_VERIFY(!tip->isVisible());
    }
    void sharedChoiceSliderSelectsAndResets() {
        QWidget window;
        window.resize(480, 360);
        ui::components::ChoiceSlider choice(&window);
        choice.addItems({"Low", "Medium", "High"});
        choice.setDescription("Model provided by the extension");
        choice.setDefaultIndex(1);
        choice.setCurrentIndex(0);
        choice.setGeometry(220, 300, 180, 28);
        QSignalSpy selected(&choice, &QComboBox::activated);
        window.show();
        QTest::mouseClick(&choice, Qt::LeftButton);
        QTRY_VERIFY(choice.popupWidget()->isVisible());
        auto* slider = choice.popupWidget()->findChild<QSlider*>("choiceSlider");
        auto* reset = choice.popupWidget()->findChild<QAbstractButton*>("choiceSliderReset");
        QVERIFY(slider && reset);
        QTest::keyClick(slider, Qt::Key_Right);
        QCOMPARE(choice.currentIndex(), 1);
        QTest::keyClick(slider, Qt::Key_Right);
        QCOMPARE(choice.currentIndex(), 2);
        QTest::mouseClick(reset, Qt::LeftButton);
        QCOMPARE(choice.currentIndex(), 1);
        QCOMPARE(selected.size(), 3);
        QVERIFY(choice.popupWidget()->isVisible());
        QTest::keyClick(slider, Qt::Key_Escape);
        QTRY_VERIFY(!choice.popupWidget()->isVisible());
    }
    void customPickerPublishesUserSelection() {
        QWidget window;
        window.resize(360, 240);
        ui::components::ComboBox combo(&window);
        combo.setPopupWithinWindow(true);
        combo.addItems({"Server model A", "Server model B"});
        combo.setGeometry(20, 200, 200, 28);
        QSignalSpy activated(&combo, &QComboBox::activated);
        window.show();
        QTest::qWait(20);
        QTest::mouseClick(&combo, Qt::LeftButton);
        QTRY_VERIFY(combo.popupWidget()->isVisible());
        auto* list = combo.popupWidget()->findChild<QListView*>();
        QVERIFY(list);
        QTest::keyClick(list, Qt::Key_Down);
        QTest::keyClick(list, Qt::Key_Return);
        QCOMPARE(combo.currentIndex(), 1);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated[0][0].toInt(), 1);
        QVERIFY(!combo.popupWidget()->isVisible());
    }
    void markdownHasReadableParagraphSpacing() {
        ui::NativeMarkdownView text(true, nullptr);
        text.setMarkdownText("First paragraph.\n\nSecond paragraph.");
        const auto first = text.document()->begin();
        QVERIFY(first.blockFormat().bottomMargin() >= 12);
        QCOMPARE(first.blockFormat().lineHeightType(), int(QTextBlockFormat::MinimumHeight));
        QCOMPARE(first.blockFormat().lineHeight(), QFontInfo(text.font()).pixelSize() * 1.5);
        QCOMPARE(first.next().blockFormat().bottomMargin(), 0.0);
        QVERIFY(first.next().text().contains("Second paragraph"));
        NativeControl root, header, divider, body, spacer;
        root.id = "sample";
        root.kind = NativeControlKind::Column;
        root.margin = 8;
        header.id = "worked";
        header.kind = NativeControlKind::Button;
        header.text = "Worked for 11m 35s $(chevron-right)";
        header.buttonStyle = NativeButtonStyle::Ghost;
        header.appearance = NativeControlAppearance::Plain;
        divider.id = "divider";
        divider.kind = NativeControlKind::Separator;
        body.id = "body";
        body.kind = NativeControlKind::Markdown;
        body.text = "Updated:\n\n- **Startup:** centered, muted Codex logo.\n- **Later loading:** "
                    "animated spinners.\n- Removed loading text.\n- Errors still show "
                    "Retry.\n\nVisually checked; Windows and Linux checks pass. Rebuilt and "
                    "restarted.\n\nEnable Codex for the updated DLL.";
        spacer.id = "spacer";
        spacer.kind = NativeControlKind::Label;
        spacer.stretch = 1;
        root.children = {header, divider, body, spacer};
        ui::NativeViewWidget view;
        view.resize(560, 400);
        view.setContent(root, 1);
        view.show();
        QTest::qWait(40);
        if (!qEnvironmentVariable("LITECODE_MARKDOWN_CAPTURE").isEmpty())
            QVERIFY(view.grab().save(qEnvironmentVariable("LITECODE_MARKDOWN_CAPTURE")));
    }
    void messageCardsRespectTheirLayoutShareAndShortTextStaysCompact() {
        NativeControl root;
        root.id = "row";
        root.kind = NativeControlKind::Row;
        root.margin = root.spacing = 0;
        NativeControl inset;
        inset.id = "inset";
        inset.kind = NativeControlKind::Label;
        inset.stretch = 3;
        NativeControl slot = root;
        slot.id = "slot";
        slot.stretch = 7;
        NativeControl alignment = inset;
        alignment.id = "alignment";
        alignment.stretch = 1;
        NativeControl card;
        card.id = "card";
        card.kind = NativeControlKind::Column;
        card.appearance = NativeControlAppearance::Card;
        card.margin = 10;
        NativeControl prompt;
        prompt.id = "prompt";
        prompt.kind = NativeControlKind::Label;
        prompt.text =
            QString("A user message that wraps naturally as its sidebar is resized. ").repeated(8);
        card.children = {prompt};
        slot.children = {alignment, card};
        root.children = {inset, slot};
        NativeControl footer;
        footer.id = "footer";
        footer.kind = NativeControlKind::Row;
        footer.margin = 0;
        footer.appearance = NativeControlAppearance::HoverActions;
        NativeControl time;
        time.id = "time";
        time.kind = NativeControlKind::Label;
        time.text = "7:58 PM";
        NativeControl edit;
        edit.id = "edit";
        edit.kind = NativeControlKind::Button;
        edit.text = "$(edit)";
        footer.children = {time, edit};
        NativeControl group;
        group.id = "group";
        group.kind = NativeControlKind::Column;
        group.appearance = NativeControlAppearance::HoverGroup;
        group.margin = 0;
        group.spacing = 4;
        group.children = {root, footer};
        NativeControl container;
        container.id = "container";
        container.kind = NativeControlKind::Column;
        container.margin = 0;
        NativeControl below = inset;
        below.id = "below";
        below.stretch = 1;
        container.children = {group, below};
        ui::NativeViewWidget view;
        view.setContent(container, 1);
        view.show();
        auto* bubble = view.findChild<QWidget*>("card");
        auto* row = view.findChild<QWidget*>("row");
        QVERIFY(bubble && row);
        for (int width : {320, 480, 720, 960, 320}) {
            view.resize(width, 600);
            QTest::qWait(30);
            const auto right = bubble->mapTo(row, QPoint(bubble->width(), 0)).x();
            QVERIFY(qAbs(right - row->width()) <= 1);
            QVERIFY2(bubble->width() <= row->width() * 0.7 + 1,
                     qPrintable(QString("Bubble %1 exceeds 70% of row %2")
                                    .arg(bubble->width())
                                    .arg(row->width())));
            QVERIFY(bubble->width() >= row->width() * 0.65);
            auto* footerWidget = view.findChild<QWidget*>("footer");
            const auto bubbleBottom = bubble->mapTo(&view, QPoint(0, bubble->height())).y();
            const auto footerTop = footerWidget->mapTo(&view, QPoint{}).y();
            auto* textLabel = view.findChild<QLabel*>("prompt");
            QVERIFY2(textLabel->height() >= textLabel->heightForWidth(textLabel->width()),
                     "The wrapped message must not be clipped by its row");
            QVERIFY2(footerTop - bubbleBottom <= 8,
                     qPrintable(QString("Bubble-to-actions gap is %1px at width %2")
                                    .arg(footerTop - bubbleBottom)
                                    .arg(width)));
        }
        for (const auto& shortText : {"cont", "Hello", "a", "go"}) {
            view.setText("prompt", shortText);
            auto* shortLabel = view.findChild<QLabel*>("prompt");
            QTRY_VERIFY(bubble->width() < 100);
            QTest::qWait(30);
            const auto diagnostic =
                QString(
                    "text=%1 bubble=%2x%3 label=%4x%5 hint=%6x%7 minimum=%8x%9 hfw=%10 line=%11")
                    .arg(shortText)
                    .arg(bubble->width())
                    .arg(bubble->height())
                    .arg(shortLabel->width())
                    .arg(shortLabel->height())
                    .arg(shortLabel->sizeHint().width())
                    .arg(shortLabel->sizeHint().height())
                    .arg(shortLabel->minimumSizeHint().width())
                    .arg(shortLabel->minimumSizeHint().height())
                    .arg(shortLabel->heightForWidth(shortLabel->width()))
                    .arg(shortLabel->fontMetrics().lineSpacing());
            QVERIFY2(shortLabel->height() <= shortLabel->fontMetrics().lineSpacing() + 2,
                     qPrintable(diagnostic));
            QVERIFY2(bubble->height() <= shortLabel->fontMetrics().lineSpacing() + 24,
                     qPrintable(diagnostic));
        }
    }
    void wireRejectsDuplicateControlsAndDeepTrees() {
        NativeControl root;
        root.id = "root";
        NativeControl button;
        button.id = "button";
        button.kind = NativeControlKind::Button;
        root.children = {button};
        QVERIFY(wire::decodeNativeControl(wire::encodeNativeControl(root)));
        auto invalidAppearance = wire::encodeNativeControl(root);
        invalidAppearance["appearance"] = 999;
        QVERIFY(!wire::decodeNativeControl(invalidAppearance));
        root.children.append(button);
        QVERIFY(!wire::decodeNativeControl(wire::encodeNativeControl(root)));
        root.children.clear();
        for (int i = 0; i < 34; ++i) {
            NativeControl parent;
            parent.id = QString::number(i);
            parent.children = {root};
            root = parent;
        }
        QVERIFY(!wire::decodeNativeControl(wire::encodeNativeControl(root)));
    }
    void delayedHostFramesPreserveNewerTypingUntilAcknowledged() {
        WorkbenchNativeViews views;
        views.setContributions(
            {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
        views.setVisible("view", true);
        QVERIFY(views.accept("owner", {{"id", "view"}, {"handle", "1"}, {"action", "register"}}));
        ui::NativeViewWidget widget;
        widget.resize(400, 200);
        widget.show();
        connect(&views, &WorkbenchNativeViews::contentChanged, &widget,
                [&](const QString&, const NativeControl& root, quint64 revision) {
                    widget.setContent(root, revision);
                });
        connect(&widget, &ui::NativeViewWidget::input, &views,
                [&](quint64 revision, const NativeViewEvent& event) {
                    views.input("view", revision, event);
                });
        NativeControl root, prompt;
        root.id = "root";
        prompt.id = "prompt";
        prompt.kind = NativeControlKind::TextEdit;
        root.children = {prompt};
        const auto frame = [&](const QString& text, const QString& ack) {
            root.children[0].text = text;
            return views.accept("owner", {{"id", "view"},
                                          {"handle", "1"},
                                          {"action", "content"},
                                          {"inputSequence", ack},
                                          {"root", wire::encodeNativeControl(root)}});
        };
        QVERIFY(frame("", "0"));
        auto* editor = widget.findChild<QPlainTextEdit*>("prompt");
        QVERIFY(editor);
        editor->setPlainText("d");
        editor->clear();
        // The host's first render only saw "d". The user has already deleted it.
        QVERIFY(frame("d", "1"));
        QCOMPARE(widget.findChild<QPlainTextEdit*>("prompt"), editor);
        QCOMPARE(editor->toPlainText(), QString{});
        QTest::keyClicks(editor, "new draft");
        QVERIFY(frame("", "2"));
        QCOMPARE(editor->toPlainText(), QString("new draft"));
        // Once all input is acknowledged, an intentional provider clear is allowed.
        QVERIFY(frame("", "11"));
        QCOMPARE(widget.findChild<QPlainTextEdit*>("prompt")->toPlainText(), QString{});
    }
    void workbenchEnforcesOwnerRevisionAndProviderLifetime() {
        WorkbenchNativeViews views;
        views.setContributions(
            {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
        QSignalSpy sends(&views, &WorkbenchNativeViews::sendRequested);
        views.setVisible("view", true);
        QVERIFY(!views.accept("other", {{"id", "view"}, {"handle", "1"}, {"action", "register"}}));
        QVERIFY(views.accept("owner", {{"id", "view"}, {"handle", "1"}, {"action", "register"}}));
        QCOMPARE(sends.size(), 1);
        NativeControl button;
        button.id = "button";
        button.kind = NativeControlKind::Button;
        QVERIFY(views.accept("owner", {{"id", "view"},
                                       {"handle", "1"},
                                       {"action", "content"},
                                       {"root", wire::encodeNativeControl(button)}}));
        views.input("view", 0, {"button", "clicked"});
        QCOMPARE(sends.size(), 1);
        views.input("view", views.revision("view"), {"button", "clicked"});
        QCOMPARE(sends.size(), 2);
        views.input("view", views.revision("view"), {"button", "selected", "Copy", 1});
        QCOMPARE(sends.size(), 3);
        const auto selection = sends.last()[1].toJsonObject();
        QCOMPARE(selection.value("type").toString(), "selected");
        QCOMPARE(selection.value("text").toString(), "Copy");
        views.removeOwner("owner");
        views.input("view", views.revision("view"), {"button", "clicked"});
        QCOMPARE(sends.size(), 3);
    }
    void rendererUsesNativeWidgetsAndDoesNotEchoProviderUpdates() {
        ui::NativeViewWidget view;
        NativeControl root;
        root.id = "root";
        NativeControl input;
        input.id = "input";
        input.kind = NativeControlKind::Input;
        root.children = {input};
        QSignalSpy events(&view, &ui::NativeViewWidget::input);
        view.setContent(root, 1);
        view.setText("input", "provider update");
        QCOMPARE(events.size(), 0);
        QVERIFY(view.findChild<QLineEdit*>("input"));
        QCOMPARE(view.findChild<QLineEdit*>("input")->text(), QString("provider update"));
    }
    void providerDisposalCanRegisterReplacementWithoutStaleTraffic() {
        WorkbenchNativeViews views;
        views.setContributions(
            {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
        bool valid = true;
        HostNativeViews host(
            [&](const QJsonObject& message) { valid = views.accept("owner", message) && valid; });
        connect(&views, &WorkbenchNativeViews::sendRequested, &host,
                [&](const QString&, const QJsonObject& message) {
                    valid = host.accept(message) && valid;
                });
        auto provider = std::make_shared<TestViewProvider>();
        auto replacement = std::make_shared<TestViewProvider>();
        auto registration = host.registerProvider("view", provider);
        views.setVisible("view", true);
        QTRY_COMPARE(provider->resolutions, 1);
        QTRY_VERIFY(views.revision("view") > 0);
        const auto oldRevision = views.revision("view");
        auto dispose = provider->view->onDidDispose()(
            [&](const std::monostate&) { host.registerProvider("view", replacement); });
        registration.dispose();
        QTRY_COMPARE(replacement->resolutions, 1);
        QTRY_VERIFY(views.revision("view") > oldRevision);
        QVERIFY(valid);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, provider->view->setText("button", "stale"));
        dispose.dispose();
    }
    void destroyingViewBackendDetachesSurvivingDocksAndActions() {
        QMainWindow window;
        QToolBar activity(&window);
        window.addToolBar(&activity);
        auto views = std::make_unique<WorkbenchNativeViews>();
        ui::NativeViewService service(*views, window, &activity);
        views->setContributions(
            {{"view", "View", "container", "Container", "activitybar", {}, "owner", {}}});
        QVERIFY(!activity.actions().isEmpty());
        const QPointer<QWidget> dock(
            window.findChild<QWidget*>("extensionContainer:owner/container"));
        QVERIFY(dock);
        views.reset();
        QTRY_VERIFY(dock.isNull());
        QVERIFY(activity.actions().isEmpty());
    }
    void extensionDetailsUsesSelectionCursorAndEditorCodeFont() {
        QTemporaryDir extensions;
        QVERIFY(extensions.isValid());
        const QDir package(extensions.filePath("litecode.codex-1.0.0"));
        QVERIFY(QDir().mkpath(package.path()));
        const QString plugin = LITECODE_CODEX_PLUGIN;
        QVERIFY(QFile::copy(plugin, package.filePath(QFileInfo(plugin).fileName())));
        QFile readmeFile(package.filePath("README.md"));
        QVERIFY(readmeFile.open(QIODevice::WriteOnly));
        readmeFile.write("Use `type: \"node\"` and see the [example link](https://example.com).\n"
                         "\n```json\n{ \"type\": \"debugpy\" }\n```\n");
        readmeFile.close();
        ExtensionManager manager(extensions.path());
        manager.discover();
        ui::ExtensionDetailsPage details(manager, "litecode.codex");
        details.resize(820, 620);
        QFont editorFont("Consolas");
        editorFont.setPixelSize(16);
        details.setCodeFont(editorFont);
        details.show();

        auto* readme = details.findChild<ui::NativeMarkdownView*>("extensionDetailsReadme");
        QVERIFY(readme && readme->isVisible());
        auto* installation = details.findChild<QWidget*>("extensionDetailsInstallation");
        auto* documentation = details.findChild<QWidget*>("extensionDetailsDocumentation");
        QVERIFY(installation && documentation);
        QVERIFY(installation->findChild<QLabel*>("extensionDetailsStatus")->isVisible());
        QVERIFY(installation->findChild<QLabel*>("extensionDetailsLocation")->isVisible());
        QCOMPARE(readme->parentWidget(), documentation);
        QVERIFY(installation->geometry().bottom() < documentation->geometry().top());
        QCOMPARE(readme->viewport()->cursor().shape(), Qt::IBeamCursor);
        QVERIFY(readme->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        const auto textPosition = [readme](const QString& text) {
            auto cursor = readme->document()->find(text);
            cursor.setPosition(cursor.selectionStart() + 1);
            return readme->cursorRect(cursor).center();
        };
        const auto hover = [readme](const QPoint& point) {
            auto* viewport = readme->viewport();
            QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
        };
        QCOMPARE(readme->anchorAt(textPosition("example link")), "https://example.com");
        QVERIFY(readme->anchorAt(textPosition("Use")).isEmpty());
        hover(textPosition("example link"));
        QCOMPARE(readme->viewport()->cursor().shape(), Qt::PointingHandCursor);
        hover(textPosition("Use"));
        QCOMPARE(readme->viewport()->cursor().shape(), Qt::IBeamCursor);

        QTRY_VERIFY(details.findChild<QPlainTextEdit*>("nativeCodeText"));
        auto* code = details.findChild<QPlainTextEdit*>("nativeCodeText");
        QCOMPARE(code->viewport()->cursor().shape(), Qt::IBeamCursor);
        QCOMPARE(code->font().family(), editorFont.family());
        QCOMPARE(code->font().pixelSize(), editorFont.pixelSize());
        editorFont.setFamily("Courier New");
        editorFont.setPixelSize(18);
        details.setCodeFont(editorFont);
        QCOMPARE(code->font().family(), editorFont.family());
        QCOMPARE(code->font().pixelSize(), editorFont.pixelSize());
        const auto inlineCode = readme->document()->find("type: \"node\"").charFormat();
        QVERIFY(inlineCode.boolProperty(ui::NativeInlineCodeProperty));
        QCOMPARE(inlineCode.font().family(), editorFont.family());
    }
    void importedCodexOwnsItsViewAcrossSeparateHostAndDisablesCleanly() {
        QTemporaryDir extensions, workspace;
        QVERIFY(extensions.isValid());
        QVERIFY(workspace.isValid());
        const auto plugin =
            qEnvironmentVariable("LITECODE_NATIVE_VIEW_TEST_PLUGIN", LITECODE_CODEX_PLUGIN);
        const QDir package(extensions.filePath("litecode.codex-1.0.0"));
        QVERIFY(QDir().mkpath(package.filePath("runtime/bin")));
        QVERIFY(QFile::copy(plugin, package.filePath(QFileInfo(plugin).fileName())));
        const auto icon = QFileInfo(plugin).dir().filePath("codex.svg");
        if (QFileInfo::exists(icon))
            QVERIFY(QFile::copy(icon, package.filePath("codex.svg")));
        const QDir icons(QFileInfo(plugin).dir().filePath("icons"));
        QVERIFY(QDir().mkpath(package.filePath("icons")));
        for (const auto& name : icons.entryList(QDir::Files))
            QVERIFY(QFile::copy(icons.filePath(name), package.filePath("icons/" + name)));
#ifdef Q_OS_WIN
        const auto runtime = package.filePath("runtime/bin/codex.exe");
#else
        const auto runtime = package.filePath("runtime/bin/codex");
#endif
        QVERIFY(QFile::copy(LITECODE_CODEX_FIXTURE, runtime));
        QString copied;
        ExtensionManager manager(extensions.path());
        manager.clipboard().setBackend([&] { return copied; },
                                       [&](const QString& text) { copied = text; });
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(workspace.path());
        QMainWindow window;
        auto* editors = new ui::EditorArea(&window);
        window.setCentralWidget(editors);
        ui::WorkbenchDocumentOpenService opens(editors, &window);
        opens.setContentProviders(&manager.contentProviders());
        ui::WorkbenchDiffCommand diffs(editors, &opens, &window);
        manager.setDiffCommandHandler([&](const QString& owner, const QJsonArray& args) {
            return diffs.execute(owner, args);
        });
        QVERIFY(manager.commands().contains("vscode.diff"));
        QSignalSpy diffResults(&manager, &ExtensionManager::commandResult);
        const auto invalidDiff =
            manager.requestCommand("vscode.diff", {"not-a-uri", "also-not-a-uri"});
        QVERIFY(invalidDiff);
        QTRY_VERIFY(!diffResults.isEmpty());
        QCOMPARE(diffResults.last()[0].toULongLong(), invalidDiff);
        QVERIFY(!diffResults.last()[2].toString().isEmpty());
        QFile changedFile(workspace.filePath("example.py"));
        QVERIFY(changedFile.open(QIODevice::WriteOnly));
        QCOMPARE(changedFile.write("new\n"), 4);
        changedFile.close();
        QToolBar activity(&window);
        window.addToolBar(&activity);
        ui::NativeViewService service(manager.nativeViews(), window, &activity);
        ui::WindowMessageService messages(manager.messages(), &window, &window);
        manager.discover();
        QCOMPARE(manager.extensions().size(), 1);
        QVERIFY2(manager.extensions().first().error.isEmpty(),
                 qPrintable(manager.extensions().first().error));
        ui::ExtensionDetailsPage details(manager, "litecode.codex");
        details.resize(760, 480);
        details.show();
        QCOMPARE(details.findChild<QLabel*>("extensionDetailsName")->text(), QString("Codex"));
        QCOMPARE(details.findChild<QLabel*>("extensionDetailsStatus")->text(), QString("Disabled"));
        QVERIFY(details.findChild<QPushButton*>("extensionDetailsEnable")->isEnabled());
        if (!qEnvironmentVariable("LITECODE_EXTENSION_DETAILS_CAPTURE").isEmpty()) {
            QTest::qWait(60);
            QVERIFY(
                details.grab().save(qEnvironmentVariable("LITECODE_EXTENSION_DETAILS_CAPTURE")));
        }
        details.hide();
        if (QFileInfo::exists(package.filePath("codex.svg"))) {
            QCOMPARE(manager.extensions().first().iconPath,
                     QFileInfo(package.filePath("codex.svg")).canonicalFilePath());
            const auto image =
                QIcon(manager.extensions().first().iconPath).pixmap(64, 64).toImage();
            QVERIFY(!image.isNull());
            QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
            int foregroundPixels = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    foregroundPixels += image.pixelColor(x, y).alpha() > 200;
            QVERIFY(foregroundPixels > 20);
            if (!qEnvironmentVariable("LITECODE_NATIVE_ICON_CAPTURE").isEmpty())
                QVERIFY(image.save(qEnvironmentVariable("LITECODE_NATIVE_ICON_CAPTURE")));
        }
        QVERIFY(manager.setEnabled("litecode.codex", true));
        QVERIFY(details.findChild<QLabel*>("extensionDetailsStatus")->text().startsWith("Enabled"));
        QCOMPARE(manager.nativeViews().contributions().first().location,
                 QString("secondarySidebar"));
        auto* action =
            window.findChild<QAction*>("extensionContainerAction:litecode.codex/litecode.codex");
        QVERIFY(action);
        window.resize(460, 800);
        window.show();
        service.openView("litecode.codex.view");
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QAbstractButton*>("codex.send") ||
                                     !manager.extensions().first().error.isEmpty(),
                                 15000);
        QVERIFY2(manager.extensions().first().error.isEmpty(),
                 qPrintable(manager.extensions().first().error));
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QComboBox*>("codex.model") &&
                                     window.findChild<QComboBox*>("codex.model")->count() == 1,
                                 15000);
        QVERIFY(manager.extensions().first().active);
        auto* prompt = window.findChild<QPlainTextEdit*>("codex.prompt");
        QVERIFY(prompt);
        QVERIFY(prompt->height() < 160);
        QVERIFY(!window.findChild<QPlainTextEdit*>("codex.details"));
        QVERIFY(!window.findChild<QAbstractButton*>("codex.stop")->isVisible());
        QVERIFY(!window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        auto* effortChoice = dynamic_cast<ui::components::ChoiceSlider*>(
            window.findChild<QComboBox*>("codex.effort"));
        QVERIFY(effortChoice);
        QTest::mouseClick(effortChoice, Qt::LeftButton);
        QTRY_VERIFY(effortChoice->popupWidget()->isVisible());
        const QPointer<QWidget> originalPopup = effortChoice->popupWidget();
        auto* fast = window.findChild<QAbstractButton*>("codex.fast");
        QVERIFY(fast && fast->isVisible());
        QVERIFY(fast->accessibleName().contains("1.5x"));
        if (!qEnvironmentVariable("LITECODE_EFFORT_CAPTURE").isEmpty()) {
            QTest::qWait(40);
            QVERIFY(originalPopup->grab().save(qEnvironmentVariable("LITECODE_EFFORT_CAPTURE")));
        }
        QTest::mouseClick(fast, Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.fast")
                        ->accessibleName()
                        .startsWith("Fast mode on"));
        QVERIFY(originalPopup && originalPopup->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.fast"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.fast")
                        ->accessibleName()
                        .startsWith("Enable fast mode"));
        QTest::keyClick(originalPopup->findChild<QSlider*>(), Qt::Key_Escape);
        auto* addContext = window.findChild<QAbstractButton*>("codex.attach");
        QVERIFY(addContext);
        addContext->click();
        auto* attachmentMenu = addContext->findChild<QMenu*>();
        QVERIFY(attachmentMenu && attachmentMenu->isVisible());
        QCOMPARE(attachmentMenu->actions().size(), 4);
        attachmentMenu->actions()[3]->trigger();
        attachmentMenu->close();
        QTRY_VERIFY(window.findChild<QWidget*>("codex.editorContext") &&
                    window.findChild<QWidget*>("codex.editorContext")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.removeEditorContext"),
                          Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<QWidget*>("codex.editorContext")->isVisible());
        // Exercise the actual descriptive menu hit areas, not just QAction::trigger.
        for (int modeIndex : {1, 2}) {
            auto* add = window.findChild<QAbstractButton*>("codex.attach");
            add->click();
            auto* menu = add->findChild<QMenu*>();
            QVERIFY(menu && menu->isVisible());
            if (modeIndex == 1 && !qEnvironmentVariable("LITECODE_ADD_CAPTURE").isEmpty())
                QVERIFY(menu->grab().save(qEnvironmentVariable("LITECODE_ADD_CAPTURE")));
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(menu->actions()[modeIndex]).center());
            QTRY_VERIFY(window.findChild<QWidget*>("codex.mode")->isVisible());
            QTRY_COMPARE(window.findChild<QLabel*>("codex.modeLabel")->text(),
                         modeIndex == 1 ? "Goal" : "Plan mode");
            QTest::mouseClick(window.findChild<QAbstractButton*>("codex.clearMode"),
                              Qt::LeftButton);
            QTRY_VERIFY(!window.findChild<QWidget*>("codex.mode")->isVisible());
        }
        auto* policy = window.findChild<QComboBox*>("codex.permissions");
        QTest::mouseClick(policy, Qt::LeftButton);
        auto* policyPopup = dynamic_cast<ui::components::ComboBox*>(policy)->popupWidget();
        auto* policyList = policyPopup->findChild<QListView*>();
        QVERIFY(policyList);
        const auto fullAccess = policyList->model()->index(2, 0);
        const auto choicesFit = QTest::qWaitFor([&] {
            return policyPopup->isVisible() &&
                   policyList->viewport()->rect().contains(policyList->visualRect(fullAccess));
        });
        const auto popupGeometry = QStringLiteral("visible=%1 window=%2x%3 popup=%4x%5 "
                                                  "viewport=%6x%7 last=%8,%9 %10x%11")
                                       .arg(policyPopup->isVisible())
                                       .arg(window.width())
                                       .arg(window.height())
                                       .arg(policyPopup->width())
                                       .arg(policyPopup->height())
                                       .arg(policyList->viewport()->width())
                                       .arg(policyList->viewport()->height())
                                       .arg(policyList->visualRect(fullAccess).x())
                                       .arg(policyList->visualRect(fullAccess).y())
                                       .arg(policyList->visualRect(fullAccess).width())
                                       .arg(policyList->visualRect(fullAccess).height());
        QVERIFY2(choicesFit, qPrintable(popupGeometry));
        QVERIFY(!policyList->verticalScrollBar()->isVisible());
        QCOMPARE(policy->itemData(2, Qt::ForegroundRole).value<QColor>(), QColor("#d99500"));
        QVERIFY(policyPopup->findChild<QPushButton*>("comboPopupHelp"));
        QVERIFY(policyList);
        QTest::keyClick(policyList, Qt::Key_End);
        QTest::keyClick(policyList, Qt::Key_Return);
        QTRY_COMPARE(window.findChild<QComboBox*>("codex.permissions")->currentIndex(), 2);
        QTest::mouseClick(window.findChild<QComboBox*>("codex.permissions"), Qt::LeftButton);
        policyList = dynamic_cast<ui::components::ComboBox*>(
                         window.findChild<QComboBox*>("codex.permissions"))
                         ->popupWidget()
                         ->findChild<QListView*>();
        QTest::keyClick(policyList, Qt::Key_Home);
        QTest::keyClick(policyList, Qt::Key_Return);
        QTRY_COMPARE(window.findChild<QComboBox*>("codex.permissions")->currentIndex(), 0);
        // Both effort and its nested model selector remain reachable at narrow widths.
        QTRY_VERIFY(window.findChild<QComboBox*>("codex.effort")->isVisible());
        QTRY_COMPARE(window.findChild<QListWidget*>("codex.recentThreads")->count(), 3);
        for (int width : {360, 600, 900, 460}) {
            window.resize(width, 800);
            QTest::qWait(40);
            auto* actions = window.findChild<QWidget*>("codex.actions");
            QVERIFY(actions);
            const auto toolbarFits = [&] {
                auto* row = window.findChild<QWidget*>("codex.actions");
                if (!row)
                    return false;
                QRect previous;
                for (const auto* id :
                     {"codex.attach", "codex.permissions", "codex.modelPicker", "codex.send"}) {
                    auto* control = row->findChild<QWidget*>(id);
                    if (!control || !control->isVisible() ||
                        !row->rect().contains(control->geometry()) ||
                        (previous.isValid() && previous.intersects(control->geometry())))
                        return false;
                    previous = control->geometry();
                }
                return true;
            };
            QTRY_VERIFY(toolbarFits());
            for (const auto* id : {"codex.effort", "codex.model", "codex.permissions"}) {
                if (QString(id) == "codex.model") {
                    QTest::mouseClick(window.findChild<QComboBox*>("codex.effort"), Qt::LeftButton);
                    QTRY_VERIFY(window.findChild<QComboBox*>("codex.model")->isVisible());
                }
                auto* combo =
                    dynamic_cast<ui::components::ComboBox*>(window.findChild<QComboBox*>(id));
                QVERIFY(combo);
                QTest::mouseClick(combo, Qt::LeftButton);
                QTRY_VERIFY(combo->popupWidget()->isVisible());
                const QRect windowBounds(window.mapToGlobal(QPoint{}), window.size());
                QVERIFY(windowBounds.contains(combo->popupWidget()->geometry()));
                if (QString(id) != "codex.model")
                    QVERIFY(combo->popupWidget()->geometry().bottom() <
                            combo->mapToGlobal(QPoint{}).y());
                QWidget* popupInput = combo->popupWidget()->findChild<QSlider*>();
                if (!popupInput)
                    popupInput = combo->popupWidget()->findChild<QListView*>();
                QVERIFY(popupInput);
                QTest::keyClick(popupInput, Qt::Key_Escape);
                QTRY_VERIFY(!combo->popupWidget()->isVisible());
                if (QString(id) == "codex.model") {
                    auto* parentChoice = dynamic_cast<ui::components::ChoiceSlider*>(
                        window.findChild<QComboBox*>("codex.effort"));
                    QTest::keyClick(parentChoice->popupWidget()->findChild<QSlider*>(),
                                    Qt::Key_Escape);
                }
            }
            auto* editor = window.findChild<QPlainTextEdit*>("codex.prompt");
            auto* composer = window.findChild<QWidget*>("codex.composer");
            window.findChild<QComboBox*>("codex.effort")->setFocus();
            QTest::mouseClick(composer, Qt::LeftButton, Qt::NoModifier, QPoint(3, 3));
            QTRY_VERIFY(window.findChild<QPlainTextEdit*>("codex.prompt")->hasFocus());
            QTest::keyClicks(editor, "draft via composer surface");
            QCOMPARE(editor->toPlainText(), "draft via composer surface");
            editor->clear();
            for (const auto& point : {QPoint(8, 8), editor->viewport()->rect().center(),
                                      editor->viewport()->rect().bottomRight() - QPoint(8, 8)}) {
                window.findChild<QComboBox*>("codex.effort")->setFocus();
                QCOMPARE(
                    window.childAt(window.mapFromGlobal(editor->viewport()->mapToGlobal(point))),
                    editor->viewport());
                QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier, point);
                QVERIFY(editor->hasFocus());
                QTest::keyClicks(editor, "typed draft");
                QCOMPARE(editor->toPlainText(), "typed draft");
                editor->clear();
            }
        }
        QTRY_VERIFY(!window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        QImage clipboardImage(12, 12, QImage::Format_ARGB32);
        clipboardImage.fill(Qt::blue);
        QApplication::clipboard()->setImage(clipboardImage);
        window.findChild<QPlainTextEdit*>("codex.prompt")->paste();
        QTRY_VERIFY(window.findChild<ui::NativeImageView*>("codex.attachment.0.name"));
        QTRY_VERIFY(
            window.findChild<ui::NativeImageView*>("codex.attachment.0.name")->imageReady());
        auto* attachmentPreview = window.findChild<ui::NativeImageView*>("codex.attachment.0.name");
        auto* attachmentRemove = window.findChild<QAbstractButton*>("codex.attachment.0.remove");
        QVERIFY(attachmentRemove && attachmentRemove->parentWidget() == attachmentPreview);
        QVERIFY(attachmentPreview->rect().contains(attachmentRemove->geometry()));
        QVERIFY(attachmentRemove->toolTip().isEmpty());
        QCOMPARE(attachmentRemove->accessibleName(), QString("Remove attachment"));
        QCOMPARE(attachmentRemove->width(), 16);
        QCOMPARE(attachmentPreview->height(), 120);
        for (const auto* id :
             {"codex.send", "codex.stop", "codex.account", "codex.permissions", "codex.prompt"}) {
            auto* control = window.findChild<QWidget*>(id);
            QVERIFY(control);
            QVERIFY2(control->toolTip().isEmpty(), id);
            QVERIFY2(!control->accessibleName().isEmpty(), id);
        }
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        QCOMPARE(window.findChild<QPlainTextEdit*>("codex.prompt")->toPlainText(), QString{});
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.attachment.0.remove"),
                          Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<ui::NativeImageView*>("codex.attachment.0.name"));
        QTRY_VERIFY2(!window.findChild<QAbstractButton*>("codex.send")->isEnabled(),
                     qPrintable("Draft after removal: [" +
                                window.findChild<QPlainTextEdit*>("codex.prompt")->toPlainText() +
                                "]"));
        QApplication::clipboard()->clear();
        QTRY_COMPARE(window.findChild<QListWidget*>("codex.recentThreads")->count(), 3);
        QVERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        QVERIFY(window.findChild<QListWidget*>("codex.recentThreads")->height() < 110);
        QVERIFY(window.findChild<QAbstractButton*>("codex.viewAllHistory")->isVisible());
        auto* recentChats = window.findChild<QListWidget*>("codex.recentThreads");
        QTest::mouseClick(recentChats->viewport(), Qt::LeftButton, Qt::NoModifier,
                          recentChats->visualItemRect(recentChats->item(0)).center());
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.openSpinner")->isVisible());
        QVERIFY(!window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        QVERIFY(!window.findChild<QWidget*>("codex.conversation")->isVisible());
        QVERIFY(!window.findChild<QWidget*>("codex.composer")->isVisible());
        QCOMPARE(window.findChild<QLabel*>("codex.title")->text(), QString("Fixture chat 0"));
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.back"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        QVERIFY(!window.findChild<QAbstractButton*>("codex.openSpinner")->isVisible());
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isEnabled());
        QTest::qWait(80);
        QVERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        recentChats = window.findChild<QListWidget*>("codex.recentThreads");
        QTest::mouseClick(recentChats->viewport(), Qt::LeftButton, Qt::NoModifier,
                          recentChats->visualItemRect(recentChats->item(0)).center());
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.openSpinner")->isVisible());
        QTRY_COMPARE(window.findChild<QLabel*>("codex.title")->text(), QString("Fixture chat"));
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.goalStatus")->isVisible());
        QVERIFY(window.findChild<QAbstractButton*>("codex.goalStatus")->height() < 40);
        QVERIFY(!window.findChild<QLabel*>("codex.goalObjective")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.goalStatus"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QLabel*>("codex.goalObjective")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.goalStatus"), Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<QLabel*>("codex.goalObjective")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.back"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        if (!qEnvironmentVariable("LITECODE_NATIVE_VIEW_CAPTURE").isEmpty())
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_NATIVE_VIEW_CAPTURE")));
        QSignalSpy inputTraffic(&manager.nativeViews(), &WorkbenchNativeViews::sendRequested);
        const auto inputDiagnostic = [&] {
            QString result;
            for (const auto& event : inputTraffic)
                result +=
                    QString::fromUtf8(
                        QJsonDocument(event[1].toJsonObject()).toJson(QJsonDocument::Compact)) +
                    '\n';
            if (auto* button = window.findChild<QAbstractButton*>("codex.send"))
                result += QString("Send: enabled=%1 visible=%2 size=%3x%4")
                              .arg(button->isEnabled())
                              .arg(button->isVisible())
                              .arg(button->width())
                              .arg(button->height());
            return result;
        };
        const auto conversationText = [&] {
            QString result;
            for (auto* text : window.findChildren<QTextBrowser*>())
                if (text->isVisible())
                    result += text->toPlainText();
            return result;
        };
        prompt = window.findChild<QPlainTextEdit*>("codex.prompt");
        QVERIFY(prompt);
        prompt->setPlainText("fixture changes request delayed-acceptance");
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.send"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.stop")->isVisible());
        window.findChild<QPlainTextEdit*>("codex.prompt")->setPlainText("draft for the next turn");
        QTRY_VERIFY2_WITH_TIMEOUT(inputTraffic.size() >= 2, qPrintable(inputDiagnostic()), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            conversationText().contains("Hello from the native Codex extension fixture."), 15000);
        QTRY_VERIFY(!window.findChild<QAbstractButton*>("codex.stop")->isVisible());
        QCOMPARE(window.findChild<QPlainTextEdit*>("codex.prompt")->toPlainText(),
                 QString("draft for the next turn"));
        window.findChild<QPlainTextEdit*>("codex.prompt")->clear();
        const auto turnAction = [&](const QString& suffix) -> QAbstractButton* {
            for (auto* button : window.findChildren<QAbstractButton*>())
                if (button->objectName().startsWith("codex.turn.") &&
                    button->objectName().endsWith(suffix))
                    return button;
            return nullptr;
        };
        if (!qEnvironmentVariable("LITECODE_NATIVE_MESSAGE_CAPTURE").isEmpty()) {
            QTest::qWait(60);
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_NATIVE_MESSAGE_CAPTURE")));
        }
        QVERIFY(turnAction(".copy"));
        auto* responseGroup = turnAction(".copy")->parentWidget()->parentWidget();
        QTest::mouseMove(responseGroup, responseGroup->rect().center());
        QVERIFY(turnAction(".tools"));
        QVERIFY(turnAction(".tools")->text().contains("Worked for 7m 56s"));
        QVERIFY(!conversationText().contains("Checking the fixture before the final answer."));
        QTest::mouseClick(turnAction(".tools"), Qt::LeftButton);
        QTRY_VERIFY(conversationText().contains("Checking the fixture before the final answer."));
        QTRY_VERIFY(turnAction(".activityGroup.1"));
        QVERIFY(!turnAction(".activityGroup.1")->icon().isNull());
        QCOMPARE(turnAction(".activityGroup.1")->iconSize(), QSize(12, 12));
        QVERIFY(!turnAction(".tool.1"));
        QTest::mouseClick(turnAction(".activityGroup.1"), Qt::LeftButton);
        QTRY_VERIFY(turnAction(".tool.1"));
        QVERIFY(turnAction(".tool.1")->text().contains("Ran echo command-0"));
        QTest::mouseClick(turnAction(".tool.1"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>(
            turnAction(".tools")->objectName().chopped(6) + ".progress.1.details"));
        QTest::mouseClick(turnAction(".activityGroup.1"), Qt::LeftButton);
        QTRY_VERIFY(!turnAction(".tool.1"));
        if (!qEnvironmentVariable("LITECODE_NATIVE_WORK_CAPTURE").isEmpty()) {
            QTest::qWait(60);
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_NATIVE_WORK_CAPTURE")));
        }
        QTest::mouseClick(turnAction(".tools"), Qt::LeftButton);
        QTRY_VERIFY(!conversationText().contains("Checking the fixture before the final answer."));
        QVERIFY(!window.findChild<QAbstractButton*>("codex.older"));
        QVERIFY(!window.findChild<QAbstractButton*>("codex.latest"));
        QTest::mouseClick(turnAction(".copy"), Qt::LeftButton);
        QTRY_VERIFY(copied.contains("Hello from the native Codex extension fixture."));
        copied.clear();
        auto* options = window.findChild<QAbstractButton*>("codex.options");
        QVERIFY(options && options->isVisible());
        options->click();
        auto* conversationMenu = options->findChild<QMenu*>();
        QVERIFY(conversationMenu && conversationMenu->isVisible());
        QCOMPARE(conversationMenu->actions().size(), 5);
        QCOMPARE(conversationMenu->actions()[2]->text(), "Copy");
        QVERIFY(conversationMenu->actions()[2]->menu());
        if (!qEnvironmentVariable("LITECODE_NATIVE_MENU_CAPTURE").isEmpty())
            QVERIFY(conversationMenu->grab().save(
                qEnvironmentVariable("LITECODE_NATIVE_MENU_CAPTURE")));
        auto* copyMenu = conversationMenu->actions()[2]->menu();
        QCOMPARE(copyMenu->actions().size(), 3);
        copyMenu->actions()[0]->trigger();
        QTRY_COMPARE(copied, QString("litecode://litecode.codex/thread/thread-fixture"));
        copyMenu->actions()[2]->trigger();
        QTRY_COMPARE(copied, QDir::toNativeSeparators(workspace.path()));
        copyMenu->actions()[1]->trigger();
        conversationMenu->close();
        QTRY_VERIFY(copied.contains("## User\n\nfixture changes request"));
        QVERIFY(copied.contains("## Assistant\n\nHello from the native Codex extension fixture."));
        const auto oldTitle = window.findChild<QLabel*>("codex.title")->text();
        options = window.findChild<QAbstractButton*>("codex.options");
        options->click();
        conversationMenu = options->findChild<QMenu*>();
        conversationMenu->actions().first()->trigger();
        conversationMenu->close();
        QTRY_VERIFY(window.findChild<QDialog*>("codex.renameDialog") &&
                    window.findChild<QDialog*>("codex.renameDialog")->isVisible());
        auto* renameInput = window.findChild<QLineEdit*>("codex.renameInput");
        QVERIFY(renameInput);
        QCOMPARE(renameInput->text(), oldTitle);
        QTRY_COMPARE(renameInput->selectedText(), oldTitle);
        renameInput->selectAll();
        QTest::keyClicks(renameInput, "   ");
        QTRY_VERIFY(!window.findChild<QAbstractButton*>("codex.renameSave")->isEnabled());
        renameInput->selectAll();
        QTest::keyClicks(renameInput, "Renamed fixture conversation");
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.renameSave")->isEnabled());
        if (!qEnvironmentVariable("LITECODE_NATIVE_RENAME_CAPTURE").isEmpty())
            QVERIFY(window.findChild<QDialog*>("codex.renameDialog")
                        ->grab()
                        .save(qEnvironmentVariable("LITECODE_NATIVE_RENAME_CAPTURE")));
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.renameSave"), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QLabel*>("codex.title")->text(),
                     QString("Renamed fixture conversation"));
        QTRY_VERIFY(!window.findChild<QDialog*>("codex.renameDialog"));
        options = window.findChild<QAbstractButton*>("codex.options");
        options->click();
        conversationMenu = options->findChild<QMenu*>();
        conversationMenu->actions().first()->trigger();
        conversationMenu->close();
        QTRY_VERIFY(window.findChild<QDialog*>("codex.renameDialog") &&
                    window.findChild<QDialog*>("codex.renameDialog")->isVisible());
        QTest::keyClick(window.findChild<QDialog*>("codex.renameDialog"), Qt::Key_Escape);
        QTRY_VERIFY(!window.findChild<QDialog*>("codex.renameDialog"));
        QCOMPARE(window.findChild<QLabel*>("codex.title")->text(),
                 QString("Renamed fixture conversation"));
        QVERIFY(turnAction(".edit"));
        auto* userGroup = turnAction(".edit")->parentWidget()->parentWidget();
        QTest::mouseMove(userGroup, userGroup->rect().center());
        auto* userActions = turnAction(".edit")->parentWidget();
        QEnterEvent userEnter(QPointF(20, 20), QPointF(20, 20), QPointF(20, 20));
        QApplication::sendEvent(userGroup, &userEnter);
        QTRY_COMPARE(static_cast<QGraphicsOpacityEffect*>(userActions->graphicsEffect())->opacity(),
                     1.0);
        QTest::mouseClick(turnAction(".edit"), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QPlainTextEdit*>("codex.prompt")->toPlainText(),
                     QString("fixture changes request delayed-acceptance"));
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.cancelEdit"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QPlainTextEdit*>("codex.prompt")->toPlainText().isEmpty());
        userGroup = turnAction(".edit")->parentWidget()->parentWidget();
        QTest::mouseMove(userGroup, userGroup->rect().center());
        QTest::mouseClick(turnAction(".edit"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QWidget*>("codex.editNotice")->isVisible());
        // Reviewing a recorded absolute file is independent of the currently
        // open workspace. It must not inherit Undo's write-boundary restriction.
        QTemporaryDir externalReview;
        QVERIFY(externalReview.isValid());
        QFile externalFile(externalReview.filePath("example.py"));
        QVERIFY(externalFile.open(QIODevice::WriteOnly));
        QCOMPARE(externalFile.write("new\n"), 4);
        externalFile.close();
        window.findChild<QPlainTextEdit*>("codex.prompt")
            ->setPlainText("fixture changes edited " + externalFile.fileName());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.send"), Qt::LeftButton);
        const auto editedPromptVisible = [&] {
            for (auto* label : window.findChildren<QLabel*>())
                if (label->objectName().endsWith(".prompt") &&
                    label->text().startsWith("fixture changes edited "))
                    return true;
            return false;
        };
        QTRY_VERIFY(editedPromptVisible());
        QTRY_VERIFY(!window.findChild<QWidget*>("codex.editNotice")->isVisible());
        QTRY_VERIFY(!window.findChild<QAbstractButton*>("codex.stop")->isVisible());
        // This turn includes an unavailable neighboring file. It must not block
        // opening example.py, and the documents must contain complete source,
        // not recorded-patch headers or a different diff presentation.
        QVERIFY(turnAction(".review"));
        QTest::mouseClick(turnAction(".review"), Qt::LeftButton);
        QTRY_VERIFY(editors->findChild<ui::DiffEditorWidget*>());
        auto* diff = editors->findChild<ui::DiffEditorWidget*>();
        auto* diffBefore = diff->findChild<editor::ScintillaEditor*>("diffOriginal");
        auto* diffAfter = diff->findChild<editor::ScintillaEditor*>("diffModified");
        QVERIFY(diffBefore && diffAfter);
        QCOMPARE(diffBefore->text(), QByteArray("old\n"));
        QCOMPARE(diffAfter->text(), QByteArray("new\n"));
        QTRY_VERIFY(!diff->hunks().isEmpty());
        if (!qEnvironmentVariable("LITECODE_NATIVE_DIFF_CAPTURE").isEmpty()) {
            QTest::qWait(60);
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_NATIVE_DIFF_CAPTURE")));
        }
        QVERIFY(!window.findChild<QPlainTextEdit*>("codex.details"));
        QVERIFY(window.findChild<QWidget*>("codex.conversation")->isVisible());
        // File links use the same route and tab identity, not a second chat surface.
        QVERIFY(turnAction(".reviewFile.0"));
        QTest::mouseClick(turnAction(".reviewFile.0"), Qt::LeftButton);
        QTRY_COMPARE(editors->findChildren<ui::DiffEditorWidget*>().size(), 1);
        QCOMPARE(editors->findChild<ui::DiffEditorWidget*>(), diff);
        window.findChild<QPlainTextEdit*>("codex.prompt")->setPlainText("approval fixture");
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.send"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QWidget*>("codex.approvalCard") &&
                                     window.findChild<QWidget*>("codex.approvalCard")->isVisible(),
                                 15000);
        auto* approval = window.findChild<QWidget*>("codex.approvalCard");
        QVERIFY(!window.findChild<QDialog*>("confirmationDialog"));
        QVERIFY(!window.findChild<QWidget*>("codex.composer")->isVisible());
        QAbstractButton* decline = nullptr;
        for (auto* button : approval->findChildren<QAbstractButton*>())
            if (button->text() == "Decline")
                decline = button;
        QVERIFY(decline);
        QTest::mouseClick(decline, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(conversationText().contains("Approval decision: decline"), 15000);
        QTRY_VERIFY(!window.findChild<QAbstractButton*>("codex.stop")->isVisible());
        window.findChild<QPlainTextEdit*>("codex.prompt")->setPlainText("wait fixture");
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.send"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.stop")->isVisible());
        const auto runningCounter = [&]() -> QLabel* {
            for (auto* label : window.findChildren<QLabel*>())
                if (label->objectName().startsWith("codex.turn.") &&
                    label->objectName().endsWith(".tools") && label->text().startsWith("Working"))
                    return label;
            return nullptr;
        };
        QTRY_VERIFY(runningCounter());
        QVERIFY(!runningCounter()->text().contains("$(chevron"));
        const auto counterId = runningCounter()->objectName();
        const auto detailsId = counterId.chopped(6) + ".workDetails";
        const auto thinkingId = counterId.chopped(6) + ".thinking";
        QTRY_VERIFY(window.findChild<QLabel*>(thinkingId));
        QVERIFY(window.findChild<QLabel*>(thinkingId)->isVisible());
        QCOMPARE(window.findChild<QLabel*>(thinkingId)->text(), "Thinking");
        auto* shimmer =
            dynamic_cast<ui::components::ShimmerLabel*>(window.findChild<QLabel*>(thinkingId));
        QVERIFY(shimmer);
        QCOMPARE(shimmer->font().pixelSize(), runningCounter()->font().pixelSize());
        if (shimmer->findChild<QTimer*>()->isActive()) {
            const auto firstFrame = shimmer->grab().toImage();
            QTRY_VERIFY(shimmer->grab().toImage() != firstFrame);
        }
        auto* thinkingTranscript =
            window.findChild<ui::NativeTranscriptView*>("codex.conversation");
        QVERIFY(thinkingTranscript);
        QTRY_VERIFY(thinkingTranscript->followingLatest());
        QTRY_VERIFY(window.findChild<QLabel*>(thinkingId)
                        ->mapTo(thinkingTranscript->viewport(), QPoint{})
                        .y() >= 0);
        QTRY_VERIFY(window.findChild<QLabel*>(thinkingId)
                            ->mapTo(thinkingTranscript->viewport(), QPoint{})
                            .y() +
                        window.findChild<QLabel*>(thinkingId)->height() <=
                    thinkingTranscript->viewport()->height());
        QTest::qWait(100); // Include deferred wrapping and later state-only frames.
        QVERIFY(thinkingTranscript->followingLatest());
        QVERIFY(window.findChild<QLabel*>(thinkingId)
                        ->mapTo(thinkingTranscript->viewport(), QPoint{})
                        .y() +
                    window.findChild<QLabel*>(thinkingId)->height() <=
                thinkingTranscript->viewport()->height());
        if (!qEnvironmentVariable("LITECODE_NATIVE_THINKING_CAPTURE").isEmpty()) {
            QVERIFY(window.grab().save(qEnvironmentVariable("LITECODE_NATIVE_THINKING_CAPTURE")));
        }
        auto* workDetails = window.findChild<QWidget*>(detailsId);
        QVERIFY(workDetails);
        QVERIFY(workDetails->isVisible());
        QTest::mouseClick(runningCounter(), Qt::LeftButton);
        QVERIFY(workDetails->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.stop"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(conversationText().contains("Turn interrupted"), 15000);
        QTRY_VERIFY(!window.findChild<QWidget*>(thinkingId) ||
                    !window.findChild<QWidget*>(thinkingId)->isVisible());
        QTRY_VERIFY(window.findChild<QAbstractButton*>(counterId));
        auto* completedDisclosure = window.findChild<QAbstractButton*>(counterId);
        QTest::mouseClick(completedDisclosure, Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<QWidget*>(detailsId)->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.history"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.threads")->isVisible());
        QTRY_COMPARE(window.findChild<QListWidget*>("codex.threads")->count(), 5);
        QVERIFY(window.findChild<QAbstractButton*>("codex.back")->isVisible());
        // The retained first page can be visible while its refresh is still
        // loading. Wait for the paging control before sending the next click.
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.moreHistory")->isEnabled());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.moreHistory"), Qt::LeftButton);
        QTRY_COMPARE(window.findChild<QListWidget*>("codex.threads")->count(), 6);
        QCOMPARE(window.findChild<QListWidget*>("codex.threads")
                     ->findItems("Fixture chat 0", Qt::MatchExactly)
                     .size(),
                 2);
        auto* historySearch = window.findChild<QLineEdit*>("codex.historySearch");
        QVERIFY(historySearch);
        QTest::keyClicks(historySearch, "missing-chat");
        QTRY_COMPARE(window.findChild<QListWidget*>("codex.threads")->count(), 0);
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.back"), Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<QListWidget*>("codex.threads")->isVisible());
        // History returns to the conversation that was open. Its Back arrow
        // opens Chats without clearing that conversation or starting a thread.
        QTRY_VERIFY(window.findChild<QWidget*>("codex.conversation")->isVisible());
        QVERIFY(conversationText().contains("Turn interrupted"));
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.back"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        QVERIFY(!window.findChild<QAbstractButton*>("codex.back")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.viewAllHistory"),
                          Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.threads")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.back"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        if (!qEnvironmentVariable("LITECODE_NATIVE_CONVERSATION_CAPTURE").isEmpty()) {
            QTest::qWait(60); // Let queued text wrapping and scrollbars settle.
            QVERIFY(
                window.grab().save(qEnvironmentVariable("LITECODE_NATIVE_CONVERSATION_CAPTURE")));
        }
        const QString literalPrompt = "# Files mentioned by the user:\n" +
                                      QString("Attachment context stays literal. ").repeated(30);
        window.findChild<QPlainTextEdit*>("codex.prompt")->setPlainText(literalPrompt);
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.send"), Qt::LeftButton);
        QTRY_VERIFY(turnAction(".expandPrompt") && turnAction(".expandPrompt")->isVisible());
        const auto renderedPrompt = [&]() -> QLabel* {
            for (auto* label : window.findChildren<QLabel*>())
                if (label->objectName().startsWith("codex.turn.") &&
                    label->objectName().endsWith(".prompt"))
                    return label;
            return nullptr;
        };
        QTRY_VERIFY(renderedPrompt());
        QCOMPARE(renderedPrompt()->textFormat(), Qt::PlainText);
        QCOMPARE(renderedPrompt()->text(), literalPrompt.left(600) + "…");
        QTest::mouseClick(turnAction(".expandPrompt"), Qt::LeftButton);
        QTRY_COMPARE(renderedPrompt()->text(), literalPrompt);
        QTest::mouseClick(turnAction(".copyPrompt"), Qt::LeftButton);
        QTRY_COMPARE(copied, literalPrompt);
        QTRY_COMPARE(turnAction(".copyPrompt")->icon().pixmap(16, 16).toImage(),
                     QIcon(package.filePath("icons/check.svg")).pixmap(16, 16).toImage());
        QTRY_COMPARE(turnAction(".copyPrompt")->icon().pixmap(16, 16).toImage(),
                     QIcon(package.filePath("icons/copy.svg")).pixmap(16, 16).toImage());
        QTest::mouseClick(turnAction(".expandPrompt"), Qt::LeftButton);
        QTRY_COMPARE(renderedPrompt()->text(), literalPrompt.left(600) + "…");
        window.findChild<QPlainTextEdit*>("codex.prompt")->setPlainText("Keep this draft");
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.back"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.history"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.threads")->isVisible());
        auto* savedChats = window.findChild<QListWidget*>("codex.threads");
        QTest::mouseClick(savedChats->viewport(), Qt::LeftButton, Qt::NoModifier,
                          savedChats->visualItemRect(savedChats->item(1)).center());
        QTRY_VERIFY(window.findChild<QLabel*>("codex.openMessage")
                        ->text()
                        .contains("another Codex session"));
        QVERIFY(!window.findChild<QPlainTextEdit*>("codex.prompt")->isReadOnly());
        QTRY_VERIFY(conversationText().contains("Saved answer from another session."));
        QTRY_COMPARE(window.findChild<QPlainTextEdit*>("codex.prompt")->toPlainText(),
                     "Keep this draft");
        QVERIFY(!window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        auto* sharedDraft = window.findChild<QPlainTextEdit*>("codex.prompt");
        QTest::mouseClick(sharedDraft->viewport(), Qt::LeftButton);
        QTest::keyClicks(sharedDraft, " still editable");
        QVERIFY(sharedDraft->toPlainText().contains("still editable"));
        QVERIFY(!window.findChild<QAbstractButton*>("codex.send")->isEnabled());
        sharedDraft->setPlainText("Keep this draft");
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.retryLocked"), Qt::LeftButton);
        QTRY_VERIFY(!window.findChild<QWidget*>("codex.locked")->isVisible() &&
                    !window.findChild<QWidget*>("codex.openStatus")->isVisible());
        QTRY_COMPARE(window.findChild<QPlainTextEdit*>("codex.prompt")->toPlainText(),
                     "Keep this draft");
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.history"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.threads")->isVisible());
        savedChats = window.findChild<QListWidget*>("codex.threads");
        QTest::mouseClick(savedChats->viewport(), Qt::LeftButton, Qt::NoModifier,
                          savedChats->visualItemRect(savedChats->item(2)).center());
        QTRY_VERIFY(
            window.findChild<QLabel*>("codex.openMessage")->text().contains("Couldn't open"));
        QVERIFY(!window.findChild<QListWidget*>("codex.threads")->isVisible());
        QVERIFY(!window.findChild<QWidget*>("codex.conversation")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.dismissOpen"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.recentThreads")->isVisible());
        QTest::mouseClick(window.findChild<QAbstractButton*>("codex.history"), Qt::LeftButton);
        QTRY_VERIFY(window.findChild<QListWidget*>("codex.threads")->isVisible());
        savedChats = window.findChild<QListWidget*>("codex.threads");
        QTest::mouseClick(savedChats->viewport(), Qt::LeftButton, Qt::NoModifier,
                          savedChats->visualItemRect(savedChats->item(3)).center());
        QTRY_VERIFY(conversationText().contains("Paged answer 23."));
        auto* transcript = window.findChild<ui::NativeTranscriptView*>("codex.conversation");
        QVERIFY(transcript);
        QTRY_VERIFY(transcript->verticalScrollBar()->maximum() > 0);
        transcript->restorePosition(0, false);
        auto* response = transcript->findChild<ui::NativeMarkdownView*>();
        QVERIFY(response);
        QWheelEvent olderWheel(
            QPointF(10, 10), QPointF(response->viewport()->mapToGlobal(QPoint(10, 10))), QPoint(),
            QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(response->viewport(), &olderWheel);
        QTRY_VERIFY2(conversationText().contains("Paged answer 9."),
                     qPrintable(inputDiagnostic() + "\nVisible transcript:\n" +
                                conversationText().left(2000)));
        QVERIFY(conversationText().contains("Paged answer 23."));
        QTest::qWait(120);
        QVERIFY(conversationText().contains("Paged answer 9."));
        transcript = window.findChild<ui::NativeTranscriptView*>("codex.conversation");
        transcript->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
        QTRY_VERIFY(conversationText().contains("Paged answer 8."));
        transcript = window.findChild<ui::NativeTranscriptView*>("codex.conversation");
        transcript->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
        QTRY_VERIFY(conversationText().contains("Paged answer 1."));
        QTest::qWait(120);
        QVERIFY(conversationText().contains("Paged answer 1."));
        transcript = window.findChild<ui::NativeTranscriptView*>("codex.conversation");
        transcript->verticalScrollBar()->setValue(transcript->verticalScrollBar()->maximum() / 2);
        auto* jumpLatest = transcript->findChild<QAbstractButton*>("nativeTranscriptLatest");
        QTRY_VERIFY(jumpLatest->isVisible());
        QTest::mouseClick(jumpLatest, Qt::LeftButton);
        QTRY_VERIFY(conversationText().contains("Paged answer 23."));
        QVERIFY(conversationText().contains("Paged answer 9."));
        QVERIFY(manager.commands().contains("litecode.codex.view.focus"));
        QVERIFY(manager.dispatchUri(QUrl("litecode://litecode.codex/thread/thread-fixture")));
        QTRY_COMPARE(window.findChild<QLabel*>("codex.title")->text(), QString("Fixture chat"));
        for (int lifecycleCycle = 0; lifecycleCycle < 5; ++lifecycleCycle) {
            const QPointer<QWidget> nativeView(
                window.findChild<QWidget*>("extensionView:litecode.codex.view"));
            QVERIFY(nativeView);
            QVERIFY(manager.setEnabled("litecode.codex", false));
            QTRY_VERIFY(nativeView.isNull());
            QVERIFY(activity.actions().isEmpty());
            QVERIFY(manager.setEnabled("litecode.codex", true));
            service.openView("litecode.codex.view");
            QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QAbstractButton*>("codex.send") ||
                                         !manager.extensions().first().error.isEmpty(),
                                     15000);
            QVERIFY2(manager.extensions().first().error.isEmpty(),
                     qPrintable(manager.extensions().first().error));
            QString reopenDiagnostic;
            for (auto* label : window.findChildren<QLabel*>())
                if (label->isVisible())
                    reopenDiagnostic += label->text().left(300) + '\n';
            QVERIFY2(window.findChild<QAbstractButton*>("codex.send"),
                     qPrintable(reopenDiagnostic));
        }
        QSignalSpy uninstalled(&manager, &ExtensionManager::uninstallFinished);
        QVERIFY(manager.uninstall("litecode.codex"));
        QTRY_COMPARE_WITH_TIMEOUT(uninstalled.size(), 1, 15000);
        QVERIFY2(uninstalled[0][1].toBool(), qPrintable(uninstalled[0][2].toString()));
        QVERIFY(manager.extensions().isEmpty());
        QVERIFY(!QFileInfo::exists(package.path()));
        QVERIFY(activity.actions().isEmpty());
    }
};
QTEST_MAIN(NativeViewTests)
#include "tst_extension_native_views.moc"
