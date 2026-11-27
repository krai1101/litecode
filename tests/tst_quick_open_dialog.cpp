#include "ui/QuickOpenDialog.h"
#include "ui/Theme.h"

#include <QLineEdit>
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
    litecode::ui::Theme::apply(*qApp, false);
    litecode::ui::QuickOpenDialog dialog(
        QStringLiteral("Change File Encoding"), QStringLiteral("Select Action"),
        QVector<litecode::ui::QuickPickItem>{
            {QStringLiteral("reopen"), QStringLiteral("Reopen with Encoding"), {}},
            {QStringLiteral("save"), QStringLiteral("Save with Encoding"), {}}});
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.show();
    QCoreApplication::processEvents();

    auto* results = dialog.findChild<QListWidget*>(QStringLiteral("quickOpenResults"));
    auto* query = dialog.findChild<QLineEdit*>(QStringLiteral("quickOpenQuery"));
    if (const auto capture = qEnvironmentVariable("LITECODE_QUICK_PICK_CAPTURE");
        !capture.isEmpty())
        QVERIFY(dialog.grab().save(capture));
    QVERIFY(query);
    QCOMPARE(query->height(), 28);
    QVERIFY(results);
    QCOMPARE(results->y() - (query->y() + query->height()), 6);
    QCOMPARE(results->count(), 2);
    const QRect itemRect = results->visualItemRect(results->item(0));
    QCOMPARE(itemRect.height(), 23);
    QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier, itemRect.center());

    QTRY_COMPARE(accepted.size(), 1);
    QCOMPARE(dialog.selectedItemId(), QStringLiteral("reopen"));
}

} // namespace litecode::tests

QTEST_MAIN(litecode::tests::QuickOpenDialogTest)
#include "tst_quick_open_dialog.moc"
