#include "ui/EditorFontDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QLabel>
#include <QListView>
#include <QtTest>

#include <algorithm>

using litecode::ui::EditorFontDialog;

class EditorFontDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void nativeListSpacingSeparatesAdjacentStates();
    void hidesWritingSystemSelector();
};

void EditorFontDialogTest::nativeListSpacingSeparatesAdjacentStates() {
    const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    EditorFontDialog dialog(font, font);

    const auto lists = dialog.findChildren<QListView*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(lists.size(), 3);
    dialog.resize(760, 540);
    dialog.show();
    QApplication::processEvents();

    int listsWithAdjacentRows = 0;
    for (const auto* list : lists) {
        QCOMPARE(list->spacing(), 1);
        if (list->model()->rowCount() < 2)
            continue;
        ++listsWithAdjacentRows;
        const QRect firstRow = list->visualRect(list->model()->index(0, 0));
        const QRect secondRow = list->visualRect(list->model()->index(1, 0));
        QVERIFY(!firstRow.isEmpty());
        QVERIFY(!secondRow.isEmpty());
        QVERIFY(secondRow.top() > firstRow.bottom() + 1);
    }
    if (listsWithAdjacentRows == 0)
        QSKIP("The headless platform did not provide adjacent font choices.");
}

void EditorFontDialogTest::hidesWritingSystemSelector() {
    const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    EditorFontDialog dialog(font, font);
    dialog.show();
    QApplication::processEvents();

    const auto* selector = dialog.findChild<QComboBox*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(selector != nullptr);
    QVERIFY(!selector->isVisible());

    const auto labels = dialog.findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
    const auto label = std::find_if(labels.cbegin(), labels.cend(), [selector](const QLabel* item) {
        return item->buddy() == selector;
    });
    QVERIFY(label != labels.cend());
    QVERIFY(!(*label)->isVisible());
}

QTEST_MAIN(EditorFontDialogTest)
#include "tst_editor_font_dialog.moc"
