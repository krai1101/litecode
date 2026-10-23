#include "ui/QuickOpenDialog.h"

#include <QListWidget>
#include <QSignalSpy>
#include <QTest>

namespace litecode::tests {

class QuickOpenDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void singleClickAcceptsStaticItem();
};

void QuickOpenDialogTest::singleClickAcceptsStaticItem() {
    litecode::ui::QuickOpenDialog dialog(
        QStringLiteral("Change File Encoding"), QStringLiteral("Select Action"),
        QVector<litecode::ui::QuickPickItem>{
            {QStringLiteral("reopen"), QStringLiteral("Reopen with Encoding"), {}}});
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.show();
    QCoreApplication::processEvents();

    auto* results = dialog.findChild<QListWidget*>(QStringLiteral("quickOpenResults"));
    QVERIFY(results);
    QCOMPARE(results->count(), 1);
    const QRect itemRect = results->visualItemRect(results->item(0));
    QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier, itemRect.center());

    QTRY_COMPARE(accepted.size(), 1);
    QCOMPARE(dialog.selectedItemId(), QStringLiteral("reopen"));
}

} // namespace litecode::tests

QTEST_MAIN(litecode::tests::QuickOpenDialogTest)
#include "tst_quick_open_dialog.moc"
