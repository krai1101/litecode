#include "editor/ScintillaEditor.h"
#include <QtTest>

using namespace litecode::editor;
class EditorTransactionTests final : public QObject {
    Q_OBJECT
  private slots:
    void defaultStopsKeepTransactionsSeparate() {
        ScintillaEditor editor;
        editor.setText("abc");
        QVERIFY(editor.beginExtensionEdit(true));
        editor.replaceBytes(1, 0, "X");
        editor.endExtensionEdit(true);
        QVERIFY(editor.beginExtensionEdit(true));
        editor.replaceBytes(2, 0, "Y");
        editor.endExtensionEdit(true);
        QCOMPARE(editor.text(), QByteArray("aXYbc"));
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("aXbc"));
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("abc"));
        editor.redo();
        QCOMPARE(editor.text(), QByteArray("aXbc"));
        editor.redo();
        QCOMPARE(editor.text(), QByteArray("aXYbc"));
    }
    void absentStopsJoinAdjacentExtensionTransactions() {
        ScintillaEditor editor;
        editor.setText("abc");
        QVERIFY(editor.beginExtensionEdit(true));
        editor.replaceBytes(1, 0, "X");
        editor.endExtensionEdit(false);
        QVERIFY(editor.beginExtensionEdit(false));
        editor.replaceBytes(2, 0, "Y");
        editor.endExtensionEdit(true);
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("abc"));
        editor.redo();
        QCOMPARE(editor.text(), QByteArray("aXYbc"));
    }
    void beforeStopClosesPreviouslyOpenTransaction() {
        ScintillaEditor editor;
        editor.setText("abc");
        QVERIFY(editor.beginExtensionEdit(true));
        editor.replaceBytes(1, 0, "X");
        editor.endExtensionEdit(false);
        QVERIFY(editor.beginExtensionEdit(true));
        editor.replaceBytes(2, 0, "Y");
        editor.endExtensionEdit(false);
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("aXbc"));
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("abc"));
    }
    void beforeFalseDoesNotReopenExplicitlyClosedTransaction() {
        ScintillaEditor editor;
        editor.setText("abc");
        QVERIFY(editor.beginExtensionEdit(true));
        editor.replaceBytes(1, 0, "X");
        editor.endExtensionEdit(true);
        QVERIFY(editor.beginExtensionEdit(false));
        editor.replaceBytes(2, 0, "Y");
        editor.endExtensionEdit(true);
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("aXbc"));
    }
    void saveAndSelectionChangesCloseOpenExtensionGroups() {
        ScintillaEditor editor;
        editor.setText("abc");
        QVERIFY(editor.beginExtensionEdit(true));
        editor.replaceBytes(1, 0, "X");
        editor.endExtensionEdit(false);
        editor.markSaved();
        QVERIFY(editor.beginExtensionEdit(false));
        editor.replaceBytes(2, 0, "Y");
        editor.endExtensionEdit(false);
        editor.selectByteRange(0, 0);
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("aXbc"));
        QVERIFY(!editor.isModified());
        editor.undo();
        QCOMPARE(editor.text(), QByteArray("abc"));
        QVERIFY(editor.isModified());
    }
    void readOnlyAndLargeFileModesRefuseTransactions() {
        ScintillaEditor editor;
        editor.setText("abc");
        editor.setReadOnly(true);
        QVERIFY(!editor.beginExtensionEdit(true));
        editor.setReadOnly(false);
        editor.setLargeFileMode(true);
        QVERIFY(!editor.beginExtensionEdit(true));
        QCOMPARE(editor.text(), QByteArray("abc"));
    }
};
QTEST_MAIN(EditorTransactionTests)
#include "tst_extension_editor_transactions.moc"
