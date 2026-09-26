#include "ui/KeyboardShortcutsDialog.h"
#include "ui/TerminalView.h"
#include "ui/commands/CommandRegistry.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QWidget>

namespace litecode::tests {
namespace {

litecode::ui::CommandSpec command(const QString& id, const QString& title,
                                  const QKeySequence& shortcut) {
    return {id, title, title, shortcut, litecode::ui::CommandSurface::ShortcutEditor, [] {}};
}

} // namespace

class KeyboardShortcutsDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void presentsSearchableVscodeStyleColumns();
    void changesRemovesAndResetsImmediately();
    void reportsConflictsWithoutChangingBindings();
    void terminalYieldsOnlyRegisteredWorkbenchShortcuts();
};

void KeyboardShortcutsDialogTest::presentsSearchableVscodeStyleColumns() {
    litecode::ui::CommandRegistry registry;
    registry.registerCommand(command(QStringLiteral("file.open"), QStringLiteral("Open File"),
                                     QKeySequence(QStringLiteral("Ctrl+O"))));
    registry.registerCommand(command(QStringLiteral("file.save"), QStringLiteral("Save"),
                                     QKeySequence(QStringLiteral("Ctrl+S"))));
    litecode::ui::KeyboardShortcutsDialog dialog(registry);

    QVERIFY(!dialog.isModal());
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("keyboardShortcutsTable"));
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("keyboardShortcutsSearch"));
    QVERIFY(table);
    QVERIFY(search);
    QCOMPARE(table->columnCount(), 3);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Command"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Keybinding"));
    QCOMPARE(table->horizontalHeaderItem(2)->text(), QStringLiteral("Source"));
    for (int column = 0; column < table->columnCount(); ++column)
        QCOMPARE(table->horizontalHeaderItem(column)->textAlignment(),
                 Qt::AlignLeft | Qt::AlignVCenter);
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("Default"));

    search->setText(QStringLiteral("Ctrl+S"));
    QVERIFY(table->isRowHidden(0));
    QVERIFY(!table->isRowHidden(1));
}

void KeyboardShortcutsDialogTest::changesRemovesAndResetsImmediately() {
    litecode::ui::CommandRegistry registry;
    const QString id = QStringLiteral("file.open");
    const QKeySequence defaultSequence(QStringLiteral("Ctrl+O"));
    registry.registerCommand(command(id, QStringLiteral("Open File"), defaultSequence));
    litecode::ui::KeyboardShortcutsDialog dialog(registry);
    QSignalSpy changed(&dialog, &litecode::ui::KeyboardShortcutsDialog::shortcutChanged);
    QSignalSpy reset(&dialog, &litecode::ui::KeyboardShortcutsDialog::shortcutReset);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("keyboardShortcutsTable"));
    auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("shortcutRemove"));
    auto* resetButton = dialog.findChild<QPushButton*>(QStringLiteral("shortcutReset"));
    QVERIFY(table);
    QVERIFY(remove);
    QVERIFY(resetButton);

    table->cellDoubleClicked(0, 1);
    auto* editor = table->cellWidget(0, 1)->findChild<QKeySequenceEdit*>();
    if (editor == nullptr)
        editor = qobject_cast<QKeySequenceEdit*>(table->cellWidget(0, 1));
    QVERIFY(editor);
    const QKeySequence replacement(QStringLiteral("Alt+O"));
    editor->setKeySequence(replacement);
    QVERIFY(QMetaObject::invokeMethod(editor, "editingFinished"));
    QCOMPARE(registry.action(id)->shortcut(), replacement);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("User"));

    table->selectRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    QVERIFY(registry.action(id)->shortcut().isEmpty());
    QCOMPARE(changed.size(), 2);

    QTest::mouseClick(resetButton, Qt::LeftButton);
    QCOMPARE(registry.action(id)->shortcut(), defaultSequence);
    QCOMPARE(reset.size(), 1);
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("Default"));
}

void KeyboardShortcutsDialogTest::reportsConflictsWithoutChangingBindings() {
    litecode::ui::CommandRegistry registry;
    const QKeySequence first(QStringLiteral("Ctrl+1"));
    const QKeySequence second(QStringLiteral("Ctrl+2"));
    registry.registerCommand(command(QStringLiteral("first"), QStringLiteral("First"), first));
    registry.registerCommand(command(QStringLiteral("second"), QStringLiteral("Second"), second));
    litecode::ui::KeyboardShortcutsDialog dialog(registry);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("keyboardShortcutsTable"));
    auto* message = dialog.findChild<QLabel*>(QStringLiteral("keyboardShortcutsMessage"));
    QVERIFY(table);
    QVERIFY(message);

    table->cellDoubleClicked(1, 1);
    auto* editor = qobject_cast<QKeySequenceEdit*>(table->cellWidget(1, 1));
    QVERIFY(editor);
    editor->setKeySequence(first);
    QVERIFY(QMetaObject::invokeMethod(editor, "editingFinished"));

    QCOMPARE(registry.action(QStringLiteral("second"))->shortcut(), second);
    QVERIFY(message->text().contains(QStringLiteral("First")));
    QCOMPARE(message->property("uiState").toString(), QStringLiteral("error"));
}

void KeyboardShortcutsDialogTest::terminalYieldsOnlyRegisteredWorkbenchShortcuts() {
    QWidget workbench;
    litecode::ui::TerminalView terminal(&workbench);
    QAction workbenchAction(&workbench);
    workbenchAction.setShortcut(QKeySequence(QStringLiteral("Ctrl+K, Ctrl+O")));
    workbench.addAction(&workbenchAction);

    QKeyEvent chordPrefix(QEvent::ShortcutOverride, Qt::Key_K, Qt::ControlModifier);
    chordPrefix.setAccepted(false);
    QApplication::sendEvent(&terminal, &chordPrefix);
    QVERIFY(!chordPrefix.isAccepted());

    QKeyEvent shellInput(QEvent::ShortcutOverride, Qt::Key_B, Qt::ControlModifier);
    shellInput.setAccepted(false);
    QApplication::sendEvent(&terminal, &shellInput);
    QVERIFY(shellInput.isAccepted());
}

} // namespace litecode::tests

QTEST_MAIN(litecode::tests::KeyboardShortcutsDialogTest)
#include "tst_keyboard_shortcuts_dialog.moc"
