#include "ui/Theme.h"
#include "ui/extensions/NativeMarkdownView.h"
#include "ui/extensions/NativeTranscriptView.h"
#include "ui/extensions/NativeViewWidget.h"
#include <QFontInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QWheelEvent>
#include <QtTest>
using namespace litecode;
using namespace litecode::extensions;
namespace {
NativeControl message(int index) {
    NativeControl node;
    node.id = QString("message.%1").arg(index);
    node.kind = NativeControlKind::Markdown;
    node.text = QString("Message %1 with **formatted text**.\n\n").arg(index).repeated(35);
    return node;
}
NativeControl frame() {
    NativeControl root, header, scroll, body, draft, send;
    root.id = "root";
    header.id = "header";
    header.kind = NativeControlKind::Label;
    header.text = "Chat";
    scroll.id = "scroll";
    scroll.kind = NativeControlKind::Scroll;
    scroll.appearance = NativeControlAppearance::Transcript;
    scroll.stretch = 1;
    body.id = "body";
    for (int i = 0; i < 8; ++i)
        body.children.append(message(i));
    scroll.children = {body};
    draft.id = "draft";
    draft.kind = NativeControlKind::TextEdit;
    draft.text = "Keep this draft";
    send.id = "send";
    send.kind = NativeControlKind::Button;
    send.text = "Send";
    root.children = {header, scroll, draft, send};
    return root;
}
} // namespace
class NativeViewReconcileTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { ui::Theme::apply(*qApp, false); }
    void repeatedCompletionFrameCannotLeavePaintingDisabled() {
        ui::NativeViewWidget view;
        view.resize(500, 600);
        view.show();
        NativeControl root, status;
        root.id = "root";
        status.id = "status";
        status.kind = NativeControlKind::Label;
        status.text = "Loading";
        root.children = {status};
        view.setContent(root, 1);
        QTRY_VERIFY(view.updatesEnabled());
        root.children[0].text = "Conversation loaded";
        view.setContent(root, 2);
        // Session completion and its follow-up state event can publish the same
        // finished frame before the queued repaint-enable callback runs.
        view.setContent(root, 3);
        QCOMPARE(view.findChild<QLabel*>("status")->text(), QString("Conversation loaded"));
        QTRY_VERIFY_WITH_TIMEOUT(view.updatesEnabled(), 1000);
    }
    void chatCodeUsesEditorFontWithoutChangingProse() {
        ui::NativeViewWidget view;
        QFont configured("Consolas");
        configured.setPixelSize(19);
        configured.setWeight(QFont::DemiBold);
        view.setCodeFont(configured);
        NativeControl markdown;
        markdown.id = "markdown";
        markdown.kind = NativeControlKind::Markdown;
        markdown.text = "Normal prose with `inline_code`.\n\n```python\nprint('hello')\n```";
        view.resize(600, 600);
        view.show();
        view.setContent(markdown, 1);
        auto* browser = view.findChild<ui::NativeMarkdownView*>("markdown");
        QVERIFY(browser);
        QTRY_VERIFY(browser->findChild<QPlainTextEdit*>("nativeCodeText"));
        auto* code = browser->findChild<QPlainTextEdit*>("nativeCodeText");
        QCOMPARE(code->font().family(), configured.family());
        QCOMPARE(code->font().pixelSize(), 19);
        QCOMPARE(code->font().weight(), QFont::DemiBold);
        const auto inlineFont = [&] {
            QTextCursor cursor(browser->document());
            const auto position = browser->document()->toPlainText().indexOf("inline_code");
            cursor.setPosition(position);
            cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
            return cursor.charFormat().font();
        };
        QCOMPARE(inlineFont().family(), configured.family());
        const int inlineSize = qMax(10, qRound(QFontInfo(browser->font()).pixelSize() * .9));
        QCOMPARE(inlineFont().pixelSize(), inlineSize);
        const auto proseFont = browser->document()->defaultFont();
        configured.setPixelSize(23);
        view.setCodeFont(configured);
        QCOMPARE(view.findChild<ui::NativeMarkdownView*>("markdown"), browser);
        QCOMPARE(browser->findChild<QPlainTextEdit*>("nativeCodeText"), code);
        QCOMPARE(code->font().pixelSize(), 23);
        QCOMPARE(inlineFont().pixelSize(), inlineSize);
        QCOMPARE(browser->document()->defaultFont(), proseFont);
    }
    void pagesKeepScrollerComposerAndOverlappingDocuments() {
        ui::NativeViewWidget view;
        view.resize(500, 800);
        view.show();
        auto root = frame();
        view.setContent(root, 1);
        QTRY_VERIFY(view.updatesEnabled());
        const QPointer<QTextBrowser> shared(view.findChild<QTextBrowser*>("message.7"));
        const QPointer<QPlainTextEdit> draft(view.findChild<QPlainTextEdit*>("draft"));
        const QPointer<ui::NativeTranscriptView> scroll(
            view.findChild<ui::NativeTranscriptView*>("scroll"));
        QVERIFY(shared && draft && scroll);
        const int documentRevision = shared->document()->revision();
        view.setText("message.7", message(7).text);
        QCOMPARE(shared->document()->revision(), documentRevision);
        root.children[1].children[0].children = {message(7), message(8)};
        view.setContent(root, 2);
        QCOMPARE(view.findChild<QTextBrowser*>("message.7"), shared.data());
        QCOMPARE(view.findChild<QPlainTextEdit*>("draft"), draft.data());
        QCOMPARE(view.findChild<ui::NativeTranscriptView*>("scroll"), scroll.data());
        QVERIFY(!view.findChild<QWidget*>("message.0"));
        QSignalSpy events(&view, &ui::NativeViewWidget::input);
        view.findChild<QPushButton*>("send")->click();
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first()[0].toULongLong(), quint64(2));
        // A repeated frame must not reparse any Markdown document.
        view.setContent(root, 3);
        QCOMPARE(view.findChild<QTextBrowser*>("message.7"), shared.data());
        QCOMPARE(view.findChild<QPlainTextEdit*>("draft"), draft.data());
    }
    void incrementalAndUserChangesCannotLeaveStaleCachedParents() {
        ui::NativeViewWidget view;
        auto root = frame();
        view.setContent(root, 1);
        view.setText("header", "Stream update");
        view.setControlEnabled("send", false);
        view.setContent(root, 2);
        QCOMPARE(view.findChild<QLabel*>("header")->text(), QString("Chat"));
        QVERIFY(view.findChild<QPushButton*>("send")->isEnabled());
        auto* draft = view.findChild<QPlainTextEdit*>("draft");
        draft->setPlainText("Edited draft");
        root.children[2].text = "Edited draft";
        view.setContent(root, 3);
        QCOMPARE(view.findChild<QPlainTextEdit*>("draft"), draft);
        QCOMPARE(draft->toPlainText(), QString("Edited draft"));
    }
    void steadyFramesDoNotRecreateHeavyContent() {
        ui::NativeViewWidget view;
        view.resize(500, 800);
        view.show();
        auto root = frame();
        view.setContent(root, 1);
        QTRY_VERIFY(view.updatesEnabled());
        auto* original = view.findChild<QTextBrowser*>("message.0");
        qint64 worst = 0;
        for (quint64 revision = 2; revision < 32; ++revision) {
            root.children[0].text = QString("Chat %1").arg(revision);
            QElapsedTimer elapsed;
            elapsed.start();
            view.setContent(root, revision);
            worst = qMax(worst, elapsed.elapsed());
            QCOMPARE(view.findChild<QTextBrowser*>("message.0"), original);
            QTest::qWait(1);
        }
        qInfo() << "Retained chat update worst (ms):" << worst;
        QVERIFY2(worst < 100, "A small header update rebuilt or blocked the transcript");
    }
    void mouseWheelMovesInFramesAndTrackpadPixelsStayDirect() {
        ui::NativeTranscriptView view;
        auto* body = new QWidget;
        body->setMinimumHeight(2400);
        view.setWidget(body);
        view.setWidgetResizable(true);
        view.resize(400, 600);
        view.show();
        QTRY_VERIFY(view.verticalScrollBar()->maximum() > 1000);
        auto* bar = view.verticalScrollBar();
        view.restorePosition(900, false);
        const int steps = qMax(1, bar->singleStep()) * QApplication::wheelScrollLines();
        const int target = 900 - qMin(steps, bar->pageStep());
        QWheelEvent mouse(QPointF(20, 20), QPointF(view.viewport()->mapToGlobal(QPoint(20, 20))),
                          QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(view.viewport(), &mouse);
        QCOMPARE(bar->value(), 900);
        QTRY_COMPARE(bar->value(), target);
        const int before = bar->value();
        QWheelEvent trackpad(QPointF(20, 20), QPointF(view.viewport()->mapToGlobal(QPoint(20, 20))),
                             QPoint(0, -17), QPoint(), Qt::NoButton, Qt::NoModifier,
                             Qt::ScrollUpdate, false);
        QApplication::sendEvent(view.viewport(), &trackpad);
        QCOMPARE(bar->value(), before + 17);
        // A restored history anchor cancels an in-flight wheel transition.
        QApplication::sendEvent(view.viewport(), &mouse);
        view.restorePosition(500, false);
        QTest::qWait(130);
        QCOMPARE(bar->value(), 500);
    }
    void smallFrameUpdatesDoNotCancelWheelMotion() {
        ui::NativeViewWidget view;
        view.resize(500, 800);
        view.show();
        auto root = frame();
        view.setContent(root, 1);
        QTRY_VERIFY(view.updatesEnabled());
        auto* scroll = view.findChild<ui::NativeTranscriptView*>("scroll");
        QVERIFY(scroll);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 1000);
        scroll->restorePosition(900, false);
        QWheelEvent mouse(QPointF(20, 20), QPointF(scroll->viewport()->mapToGlobal(QPoint(20, 20))),
                          QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(scroll->viewport(), &mouse);
        root.children[0].text = "Updated chat title";
        view.setContent(root, 2);
        QCOMPARE(view.findChild<ui::NativeTranscriptView*>("scroll"), scroll);
        QTRY_VERIFY(scroll->verticalScrollBar()->value() < 900);
    }
    void newerNavigationWinsOverQueuedHistoryRestore() {
        ui::NativeViewWidget view;
        view.resize(500, 800);
        view.show();
        auto root = frame();
        view.setContent(root, 1);
        QTRY_VERIFY(view.updatesEnabled());
        auto* scroll = view.findChild<ui::NativeTranscriptView*>("scroll");
        QVERIFY(scroll);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 1000);
        scroll->restorePosition(900, false);
        root.children[1].children[0].children.append(message(8));
        view.setContent(root, 2);
        scroll->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
        QTRY_VERIFY(view.updatesEnabled());
        QTest::qWait(30);
        QCOMPARE(scroll->verticalScrollBar()->value(), 0);
    }
};
QTEST_MAIN(NativeViewReconcileTests)
#include "tst_native_view_reconcile.moc"
