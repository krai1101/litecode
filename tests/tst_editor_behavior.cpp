#include "core/FileSystemPath.h"
#include "editor/DocumentSession.h"
#include "editor/ScintillaEditor.h"
#include "ui/EditorArea.h"
#include "ui/components/FindTextInput.h"

#include <SciLexer.h>
#include <ScintillaEditBase.h>
#include <ScintillaMessages.h>

#include <QFile>
#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

namespace {

bool writeBytes(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

unsigned int scintillaMessage(Scintilla::Message message) {
    return static_cast<unsigned int>(message);
}

int styleAt(ScintillaEditBase* control, const QByteArray& contents, const QByteArray& token,
            qsizetype tokenOffset = 0) {
    const qsizetype position = contents.indexOf(token);
    if (position < 0)
        return -1;
    return static_cast<int>(
        control->send(scintillaMessage(Scintilla::Message::GetStyleAt), position + tokenOffset));
}

} // namespace

class EditorBehaviorTest final : public QObject {
    Q_OBJECT

  private slots:
    void insertedLinesUseDocumentLineEnding_data();
    void insertedLinesUseDocumentLineEnding();
    void findWidgetSupportsCountsNavigationAndEscape();
    void findWidgetSeedsSelectionAndWordAtCaret();
    void goToMatchUsesUtf16Columns();
    void findInSelectionLimitsNavigationAndReplacement();
    void findAndReplaceAcrossLines();
    void failedAutomaticReloadIsReported();
    void loadedSnapshotDetectsLaterDiskChanges();
    void largeFileModeIsExplicit();
    void readOnlyEditorRejectsEdits();
    void readOnlyFileStateReachesDocumentSession();
    void reloadPreservesCursorAndUndoHistory();
    void replaceOneAdvancesToNextMatch();
    void findCountReportsLimit();
    void largeFileFindCountRunsAsynchronously();
    void reopeningFindRefreshesAfterHiddenEdits();
    void cleanSaveDoesNotRewriteMixedLineEndings();
    void findCountFollowsEditsAndActiveTab();
    void transientDeletionDoesNotOrphanTab();
    void pathIdentityFollowsFileSystemCaseSensitivity();
    void cppLexerUsesConfiguredLexillaCapabilities();
    void nativeLexersReceiveConfiguredKeywords();
};

void EditorBehaviorTest::cppLexerUsesConfiguredLexillaCapabilities() {
    const QByteArray contents = QByteArrayLiteral("/** @param value description */\n"
                                                  "int main() {\n"
                                                  "    QCoreApplication application;\n"
                                                  "    const char* text = \"line\\n\";\n"
                                                  "    // TODO improve\n"
                                                  "}\n");
    litecode::editor::ScintillaEditor editor;
    editor.setFilePath(QStringLiteral("main.cpp"));
    editor.setText(contents);
    auto* control = editor.findChild<ScintillaEditBase*>(QStringLiteral("scintillaControl"));
    QVERIFY(control);
    control->send(scintillaMessage(Scintilla::Message::Colourise), 0, -1);

    QCOMPARE(styleAt(control, contents, QByteArrayLiteral("@param")), SCE_C_COMMENTDOCKEYWORD);
    QCOMPARE(styleAt(control, contents, QByteArrayLiteral("QCoreApplication")), SCE_C_GLOBALCLASS);
    QCOMPARE(styleAt(control, contents, QByteArrayLiteral("\\n")), SCE_C_ESCAPESEQUENCE);
    QCOMPARE(styleAt(control, contents, QByteArrayLiteral("TODO")), SCE_C_TASKMARKER);
}

void EditorBehaviorTest::nativeLexersReceiveConfiguredKeywords() {
    const auto verify = [](const QString& path, const QByteArray& contents, const QByteArray& token,
                           int expectedStyle) {
        litecode::editor::ScintillaEditor editor;
        editor.setFilePath(path);
        editor.setText(contents);
        auto* control = editor.findChild<ScintillaEditBase*>(QStringLiteral("scintillaControl"));
        QVERIFY(control);
        control->send(scintillaMessage(Scintilla::Message::Colourise), 0, -1);
        QCOMPARE(styleAt(control, contents, token), expectedStyle);
    };

    verify(QStringLiteral("sample.py"), QByteArrayLiteral("print('ok')\n"),
           QByteArrayLiteral("print"), SCE_P_WORD2);
    verify(QStringLiteral("sample.rs"), QByteArrayLiteral("let value: u32 = 1;\n"),
           QByteArrayLiteral("u32"), SCE_RUST_WORD2);
    verify(QStringLiteral("sample.sh"), QByteArrayLiteral("if true; then echo ok; fi\n"),
           QByteArrayLiteral("if"), SCE_SH_WORD);
    verify(QStringLiteral("sample.sql"), QByteArrayLiteral("select value from items;\n"),
           QByteArrayLiteral("select"), SCE_SQL_WORD);
    verify(QStringLiteral("CMakeLists.txt"), QByteArrayLiteral("add_executable(app main.cpp)\n"),
           QByteArrayLiteral("add_executable"), SCE_CMAKE_COMMANDS);
    verify(QStringLiteral("sample.ps1"), QByteArrayLiteral("function Invoke-Sample {}\n"),
           QByteArrayLiteral("function"), SCE_POWERSHELL_KEYWORD);
    verify(QStringLiteral("sample.lua"), QByteArrayLiteral("print('ok')\n"),
           QByteArrayLiteral("print"), SCE_LUA_WORD2);
    verify(QStringLiteral("sample.bat"), QByteArrayLiteral("echo ok\n"), QByteArrayLiteral("echo"),
           SCE_BAT_WORD);
    verify(QStringLiteral("sample.asm"), QByteArrayLiteral("mov eax, ebx\n"),
           QByteArrayLiteral("mov"), SCE_ASM_CPUINSTRUCTION);
    verify(QStringLiteral("sample.fs"), QByteArrayLiteral("let value = 1\n"),
           QByteArrayLiteral("let"), SCE_FSHARP_KEYWORD);
    verify(QStringLiteral("sample.coffee"), QByteArrayLiteral("class Sample\n"),
           QByteArrayLiteral("class"), SCE_COFFEESCRIPT_WORD);
    verify(QStringLiteral("sample.jl"), QByteArrayLiteral("function sample()\nend\n"),
           QByteArrayLiteral("function"), SCE_JULIA_KEYWORD1);
    verify(QStringLiteral("sample.pl"), QByteArrayLiteral("sub sample {}\n"),
           QByteArrayLiteral("sub"), SCE_PL_WORD);
    verify(QStringLiteral("sample.f90"), QByteArrayLiteral("program sample\nend program\n"),
           QByteArrayLiteral("program"), SCE_F_WORD);
    verify(QStringLiteral("sample.lisp"), QByteArrayLiteral("(defun sample () nil)\n"),
           QByteArrayLiteral("defun"), SCE_LISP_KEYWORD);
    verify(QStringLiteral("sample.pas"), QByteArrayLiteral("begin end.\n"),
           QByteArrayLiteral("begin"), SCE_PAS_WORD);
    verify(QStringLiteral("sample.vb"), QByteArrayLiteral("Dim value As Integer\n"),
           QByteArrayLiteral("Dim"), SCE_B_KEYWORD);
    verify(QStringLiteral("sample.zig"), QByteArrayLiteral("fn sample() void {}\n"),
           QByteArrayLiteral("fn"), SCE_ZIG_KW_PRIMARY);
    verify(QStringLiteral("sample.nim"), QByteArrayLiteral("proc sample() = discard\n"),
           QByteArrayLiteral("proc"), SCE_NIM_WORD);
    verify(QStringLiteral("sample.d"), QByteArrayLiteral("module sample;\n"),
           QByteArrayLiteral("module"), SCE_D_WORD);
    verify(QStringLiteral("sample.tcl"), QByteArrayLiteral("proc sample {} {}\n"),
           QByteArrayLiteral("proc"), SCE_TCL_WORD);
    verify(QStringLiteral("sample.v"), QByteArrayLiteral("module sample; endmodule\n"),
           QByteArrayLiteral("module"), SCE_V_WORD);
    verify(QStringLiteral("sample.vhd"), QByteArrayLiteral("entity sample is end sample;\n"),
           QByteArrayLiteral("entity"), SCE_VHDL_KEYWORD);
    verify(QStringLiteral("sample.gradle"), QByteArrayLiteral("class Sample {}\n"),
           QByteArrayLiteral("class"), SCE_C_WORD);
    verify(QStringLiteral("sample.json"), QByteArrayLiteral("{\"enabled\": true}\n"),
           QByteArrayLiteral("true"), SCE_JSON_KEYWORD);
    verify(QStringLiteral("sample.json"), QByteArrayLiteral("{\"text\": \"line\\n\"}\n"),
           QByteArrayLiteral("\\n"), SCE_JSON_ESCAPESEQUENCE);
    verify(QStringLiteral("sample.jsonc"), QByteArrayLiteral("// note\n{\"text\": \"line\\n\"}\n"),
           QByteArrayLiteral("note"), SCE_JSON_LINECOMMENT);
    verify(QStringLiteral("sample.scss"), QByteArrayLiteral("$color: red; // note\n"),
           QByteArrayLiteral("color"), SCE_CSS_VARIABLE);
    verify(QStringLiteral("sample.scss"), QByteArrayLiteral("$color: red; // note\n"),
           QByteArrayLiteral("note"), SCE_CSS_COMMENT);
    verify(QStringLiteral("sample.less"), QByteArrayLiteral("value { color: @accent; }\n"),
           QByteArrayLiteral("accent"), SCE_CSS_VARIABLE);
    verify(QStringLiteral("sample.yaml"), QByteArrayLiteral("enabled: true\n"),
           QByteArrayLiteral("true"), SCE_YAML_KEYWORD);
    verify(QStringLiteral("sample.toml"), QByteArrayLiteral("enabled = true\n"),
           QByteArrayLiteral("true"), SCE_TOML_KEYWORD);
    verify(QStringLiteral("sample.dart"), QByteArrayLiteral("List<int> values = [];\n"),
           QByteArrayLiteral("List"), SCE_DART_KW_TYPE);
    verify(QStringLiteral("sample.r"), QByteArrayLiteral("mean(values)\n"),
           QByteArrayLiteral("mean"), SCE_R_BASEKWORD);
    verify(QStringLiteral("sample.r"), QByteArrayLiteral("value <- \"line\\n\"\n"),
           QByteArrayLiteral("\\n"), SCE_R_ESCAPESEQUENCE);
    verify(QStringLiteral("sample.ps1"), QByteArrayLiteral("gci .\n"), QByteArrayLiteral("gci"),
           SCE_POWERSHELL_ALIAS);
    verify(QStringLiteral("sample.html"), QByteArrayLiteral("<section></section>\n"),
           QByteArrayLiteral("section"), SCE_H_TAG);
}

void EditorBehaviorTest::insertedLinesUseDocumentLineEnding_data() {
    QTest::addColumn<int>("lineEnding");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("lf") << static_cast<int>(litecode::core::LineEnding::Lf)
                        << QByteArrayLiteral("one\ntwo");
    QTest::newRow("crlf") << static_cast<int>(litecode::core::LineEnding::CrLf)
                          << QByteArrayLiteral("one\r\ntwo");
    QTest::newRow("cr") << static_cast<int>(litecode::core::LineEnding::Cr)
                        << QByteArrayLiteral("one\rtwo");
}

void EditorBehaviorTest::insertedLinesUseDocumentLineEnding() {
    QFETCH(int, lineEnding);
    QFETCH(QByteArray, expected);
    litecode::editor::ScintillaEditor editor;
    editor.resize(500, 300);
    editor.setLineEnding(static_cast<litecode::core::LineEnding>(lineEnding));
    editor.setText(QByteArrayLiteral("one"));
    editor.show();
    QWidget* control = editor.findChild<QWidget*>(QStringLiteral("scintillaControl"));
    QVERIFY(control);
    control->setFocus();
    QTest::keyClick(control, Qt::Key_End);
    QTest::keyClick(control, Qt::Key_Return);
    QTest::keyClicks(control, QStringLiteral("two"));
    QCOMPARE(editor.text(), expected);
}

void EditorBehaviorTest::findWidgetSupportsCountsNavigationAndEscape() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("find.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("alpha beta alpha")));

    litecode::ui::EditorArea area;
    area.resize(800, 500);
    area.show();
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    area.showFindReplace(false);

    auto* input =
        area.findChild<litecode::ui::components::FindTextInput*>(QStringLiteral("findInput"));
    auto* result = area.findChild<QLabel*>(QStringLiteral("findResultLabel"));
    auto* bar = area.findChild<QFrame*>(QStringLiteral("findReplaceBar"));
    QVERIFY(input);
    QVERIFY(result);
    QVERIFY(bar);
    QCOMPARE(input->document()->documentMargin(), 0.0);
    QTRY_VERIFY(input->viewport()->height() >= QFontMetrics(input->font()).lineSpacing());
    input->setText(QStringLiteral("alpha"));
    QTRY_COMPARE(result->text(), QStringLiteral("1 of 2"));
    QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(result->text(), QStringLiteral("2 of 2"));
    QTest::keyClick(input, Qt::Key_Escape);
    QVERIFY(!bar->isVisible());
}

void EditorBehaviorTest::findWidgetSeedsSelectionAndWordAtCaret() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("seed.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("alpha beta alpha")));
    litecode::ui::EditorArea area;
    area.resize(800, 500);
    area.show();
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    auto* editor = area.findChild<litecode::editor::ScintillaEditor*>();
    auto* input =
        area.findChild<litecode::ui::components::FindTextInput*>(QStringLiteral("findInput"));
    auto* selectionButton = area.findChild<QToolButton*>(QStringLiteral("findSelectionButton"));
    QVERIFY(editor);
    QVERIFY(input);
    QVERIFY(selectionButton);
    editor->selectByteRange(6, 10);
    area.showFindReplace(false);
    QCOMPARE(input->text(), QStringLiteral("beta"));
    QVERIFY(selectionButton->isEnabled());
    selectionButton->click();
    input->setText(QStringLiteral("alpha"));
    auto* result = area.findChild<QLabel*>(QStringLiteral("findResultLabel"));
    QVERIFY(result);
    QCOMPARE(result->text(), QStringLiteral("No results"));
    selectionButton->click();
    QTRY_VERIFY(result->text().endsWith(QStringLiteral(" of 2")));
    QTest::keyClick(input, Qt::Key_Escape);
    editor->selectByteRange(2, 2);
    area.showFindReplace(false);
    QCOMPARE(input->text(), QStringLiteral("alpha"));
    input->setText(QStringLiteral("draft"));
    QTest::keyClick(input, Qt::Key_Up);
    QCOMPARE(input->text(), QStringLiteral("alpha"));
    QTest::keyClick(input, Qt::Key_Down);
    QCOMPARE(input->text(), QStringLiteral("draft"));
}

void EditorBehaviorTest::goToMatchUsesUtf16Columns() {
    litecode::editor::ScintillaEditor editor;
    editor.setText(QStringLiteral("😀 café foo\nother").toUtf8());
    editor.goToMatch(1, 9, 3);
    QCOMPARE(editor.selectedText(), QStringLiteral("foo"));
}

void EditorBehaviorTest::findInSelectionLimitsNavigationAndReplacement() {
    litecode::editor::ScintillaEditor editor;
    editor.setText(QByteArrayLiteral("foo foo foo"));
    editor.setFindScope(QPair<qint64, qint64>{4, 7});
    QCOMPARE(editor.findMatchStatus(QStringLiteral("foo"), true, false).total, 1);
    QVERIFY(editor.findNext(QStringLiteral("foo"), true, false));
    QCOMPARE(editor.selectionByteRange(), (QPair<qint64, qint64>{4, 7}));
    QCOMPARE(editor.replaceAll(QStringLiteral("foo"), QStringLiteral("bar"), true, false), 1);
    QCOMPARE(editor.text(), QByteArrayLiteral("foo bar foo"));
    editor.setFindScope(std::nullopt);
    QCOMPARE(editor.findMatchStatus(QStringLiteral("foo"), true, false).total, 2);
}

void EditorBehaviorTest::findAndReplaceAcrossLines() {
    litecode::editor::ScintillaEditor direct;
    direct.setText(QByteArrayLiteral("alpha\nbeta gamma"));
    QVERIFY(direct.findNext(QStringLiteral("alpha\nbeta"), true, false));
    QCOMPARE(direct.selectedText(), QStringLiteral("alpha\nbeta"));
    QVERIFY(direct.findPrevious(QStringLiteral("alpha\nbeta"), true, false));
    QCOMPARE(direct.selectedText(), QStringLiteral("alpha\nbeta"));
    QVERIFY(direct.replaceNext(QStringLiteral("alpha\nbeta"), QStringLiteral("first\nsecond"), true,
                               false));
    QCOMPARE(direct.text(), QByteArrayLiteral("first\nsecond gamma"));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("multiline.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("alpha\nbeta gamma")));
    litecode::ui::EditorArea area;
    area.resize(800, 500);
    area.show();
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    area.showFindReplace(true);
    auto* find =
        area.findChild<litecode::ui::components::FindTextInput*>(QStringLiteral("findInput"));
    auto* replace =
        area.findChild<litecode::ui::components::FindTextInput*>(QStringLiteral("replaceInput"));
    auto* result = area.findChild<QLabel*>(QStringLiteral("findResultLabel"));
    auto* editor = area.findChild<litecode::editor::ScintillaEditor*>();
    QVERIFY(find);
    QVERIFY(replace);
    QVERIFY(result);
    QVERIFY(editor);
    QCOMPARE(editor->text(), QByteArrayLiteral("alpha\nbeta gamma"));
    find->setText(QStringLiteral("alpha\nbeta"));
    QCOMPARE(find->text(), QStringLiteral("alpha\nbeta"));
    QCOMPARE(editor->selectedText(), QStringLiteral("alpha\nbeta"));
    QCOMPARE(editor->findMatchStatus(QStringLiteral("alpha\nbeta"), false, false).current, 1);
    QTRY_COMPARE(result->text(), QStringLiteral("1 of 1"));
    find->moveCursor(QTextCursor::End);
    QTest::keyClick(find, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(find->text(), QStringLiteral("alpha\nbeta\n"));
    find->setText(QStringLiteral("alpha\nbeta"));
    replace->setText(QStringLiteral("first\nsecond"));
    for (QToolButton* button : area.findChildren<QToolButton*>()) {
        if (button->toolTip() == QStringLiteral("Replace All")) {
            button->click();
            break;
        }
    }
    QCOMPARE(editor->text(), QByteArrayLiteral("first\nsecond gamma"));
}

void EditorBehaviorTest::failedAutomaticReloadIsReported() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("external.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("safe text")));

    litecode::ui::EditorArea area;
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QSignalSpy failed(&area, &litecode::ui::EditorArea::fileReloadFailed);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    QVERIFY(writeBytes(path, QByteArray("binary\0payload", 14)));
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
    auto* editor = area.findChild<litecode::editor::ScintillaEditor*>();
    QVERIFY(editor);
    QCOMPARE(editor->text(), QByteArrayLiteral("safe text"));
    auto* tabs = area.findChild<QTabWidget*>();
    QVERIFY(tabs);
    QVERIFY(tabs->tabToolTip(0).contains(QStringLiteral("Reload failed")));
    auto* banner = area.findChild<QFrame*>(QStringLiteral("editorReloadErrorBanner"));
    QVERIFY(banner);
    QVERIFY(!banner->isHidden());
}

void EditorBehaviorTest::loadedSnapshotDetectsLaterDiskChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("snapshot.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("first")));
    const litecode::editor::DocumentLoadResult load =
        litecode::editor::DocumentSession::loadBounded(path);
    QVERIFY(litecode::editor::DocumentSession::matchesDiskSnapshot(path, load));
    QVERIFY(writeBytes(path, QByteArrayLiteral("changed-size")));
    QVERIFY(!litecode::editor::DocumentSession::matchesDiskSnapshot(path, load));
}

void EditorBehaviorTest::largeFileModeIsExplicit() {
    litecode::editor::ScintillaEditor editor;
    QVERIFY(!editor.isLargeFileMode());
    editor.setLargeFileMode(true);
    QVERIFY(editor.isLargeFileMode());
}

void EditorBehaviorTest::readOnlyEditorRejectsEdits() {
    litecode::editor::ScintillaEditor editor;
    editor.resize(500, 300);
    editor.setText(QByteArrayLiteral("unchanged"));
    editor.setReadOnly(true);
    QVERIFY(editor.isReadOnly());
    editor.show();
    QWidget* control = editor.findChild<QWidget*>(QStringLiteral("scintillaControl"));
    QVERIFY(control);
    control->setFocus();
    QTest::keyClick(control, Qt::Key_End);
    QTest::keyClicks(control, QStringLiteral("x"));
    QCOMPARE(editor.text(), QByteArrayLiteral("unchanged"));
}

void EditorBehaviorTest::readOnlyFileStateReachesDocumentSession() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("readonly.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("locked")));
    const QFileDevice::Permissions originalPermissions = QFile::permissions(path);
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser |
                                            QFileDevice::ReadGroup | QFileDevice::ReadOther));

    const litecode::editor::DocumentLoadResult load =
        litecode::editor::DocumentSession::loadBounded(path);
    QVERIFY(load.readOnly);
    litecode::editor::DocumentSession document(litecode::core::DocumentId(1), path);
    QVERIFY(document.acceptLoad(load));
    QVERIFY(document.isReadOnly());

    QVERIFY(QFile::setPermissions(path, originalPermissions));
}

void EditorBehaviorTest::reloadPreservesCursorAndUndoHistory() {
    litecode::editor::ScintillaEditor editor;
    editor.resize(500, 300);
    editor.setText(QByteArrayLiteral("abcdef"));
    editor.show();
    QWidget* control = editor.findChild<QWidget*>(QStringLiteral("scintillaControl"));
    QVERIFY(control);
    control->setFocus();
    QTest::keyClick(control, Qt::Key_Home);
    for (int index = 0; index < 3; ++index)
        QTest::keyClick(control, Qt::Key_Right);

    editor.reloadText(QByteArrayLiteral("uvwxyz"));
    QTest::keyClicks(control, QStringLiteral("X"));
    QCOMPARE(editor.text(), QByteArrayLiteral("uvwXxyz"));
    editor.undo();
    QCOMPARE(editor.text(), QByteArrayLiteral("uvwxyz"));
    editor.undo();
    QCOMPARE(editor.text(), QByteArrayLiteral("abcdef"));
}

void EditorBehaviorTest::replaceOneAdvancesToNextMatch() {
    litecode::editor::ScintillaEditor editor;
    editor.setText(QByteArrayLiteral("alpha beta alpha"));
    QVERIFY(editor.findNext(QStringLiteral("alpha"), true, false));
    QVERIFY(editor.replaceNext(QStringLiteral("alpha"), QStringLiteral("one"), true, false));
    QCOMPARE(editor.text(), QByteArrayLiteral("one beta alpha"));
    QCOMPARE(editor.selectedText(), QStringLiteral("alpha"));
}

void EditorBehaviorTest::findCountReportsLimit() {
    litecode::editor::ScintillaEditor editor;
    QByteArray contents;
    contents.reserve(40'002);
    for (int index = 0; index < 20'000; ++index)
        contents.append("x ");
    editor.setText(contents);
    const litecode::editor::FindMatchStatus status =
        editor.findMatchStatus(QStringLiteral("x"), true, true);
    QCOMPARE(status.total, 19'999);
    QVERIFY(status.limitHit);
}

void EditorBehaviorTest::largeFileFindCountRunsAsynchronously() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("large-find.txt"));
    QByteArray contents(4 * 1024 * 1024 + 128, 'x');
    contents.replace(0, 6, QByteArrayLiteral("needle"));
    contents.replace(contents.size() - 6, 6, QByteArrayLiteral("needle"));
    QVERIFY(writeBytes(path, contents));

    litecode::ui::EditorArea area;
    area.resize(800, 500);
    area.show();
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    area.showFindReplace(false);
    auto* input =
        area.findChild<litecode::ui::components::FindTextInput*>(QStringLiteral("findInput"));
    auto* result = area.findChild<QLabel*>(QStringLiteral("findResultLabel"));
    QVERIFY(input);
    QVERIFY(result);
    input->setText(QStringLiteral("needle"));
    QTRY_COMPARE_WITH_TIMEOUT(result->text(), QStringLiteral("1 of 2"), 5000);
    auto* editor = area.findChild<litecode::editor::ScintillaEditor*>();
    QVERIFY(editor);
    editor->selectByteRange(100, 100);
    input->setText(QStringLiteral("need"));
    editor->selectByteRange(150, 150);
    QTRY_COMPARE_WITH_TIMEOUT(result->text(), QStringLiteral("2 results"), 5000);
    QCOMPARE(editor->selectionByteRange(), (QPair<qint64, qint64>{150, 150}));
    editor->setText(QByteArray(4 * 1024 * 1024 + 128, 'x'));
    QTRY_COMPARE_WITH_TIMEOUT(result->text(), QStringLiteral("No results"), 5000);
}

void EditorBehaviorTest::reopeningFindRefreshesAfterHiddenEdits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("find-reopen.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("alpha alpha")));

    litecode::ui::EditorArea area;
    area.resize(800, 500);
    area.show();
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    area.showFindReplace(false);
    auto* input =
        area.findChild<litecode::ui::components::FindTextInput*>(QStringLiteral("findInput"));
    auto* result = area.findChild<QLabel*>(QStringLiteral("findResultLabel"));
    auto* editor = area.findChild<litecode::editor::ScintillaEditor*>();
    QVERIFY(input);
    QVERIFY(result);
    QVERIFY(editor);
    input->setText(QStringLiteral("alpha"));
    QTRY_COMPARE_WITH_TIMEOUT(result->text(), QStringLiteral("1 of 2"), 5000);
    QTest::keyClick(input, Qt::Key_Escape);
    editor->setText(QByteArrayLiteral("alpha"));
    area.showFindReplace(false);
    QTRY_VERIFY_WITH_TIMEOUT(result->text().contains(QStringLiteral("of 1")) ||
                                 result->text() == QStringLiteral("1 results"),
                             5000);
}

void EditorBehaviorTest::cleanSaveDoesNotRewriteMixedLineEndings() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("mixed.txt"));
    const QByteArray original = QByteArrayLiteral("one\r\ntwo\nthree\r\n");
    QVERIFY(writeBytes(path, original));

    litecode::ui::EditorArea area;
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    QVERIFY(area.saveCurrent());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
}

void EditorBehaviorTest::findCountFollowsEditsAndActiveTab() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.txt"));
    const QString second = directory.filePath(QStringLiteral("second.txt"));
    QVERIFY(writeBytes(first, QByteArrayLiteral("alpha alpha")));
    QVERIFY(writeBytes(second, QByteArrayLiteral("alpha alpha alpha")));

    litecode::ui::EditorArea area;
    area.resize(800, 500);
    area.show();
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(first));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    area.showFindReplace(false);
    auto* input =
        area.findChild<litecode::ui::components::FindTextInput*>(QStringLiteral("findInput"));
    auto* result = area.findChild<QLabel*>(QStringLiteral("findResultLabel"));
    auto* editor = area.findChild<litecode::editor::ScintillaEditor*>();
    QVERIFY(input);
    QVERIFY(result);
    QVERIFY(editor);
    input->setText(QStringLiteral("alpha"));
    QTRY_COMPARE(result->text(), QStringLiteral("1 of 2"));
    editor->setText(QByteArrayLiteral("alpha"));
    QTRY_VERIFY_WITH_TIMEOUT(result->text().contains(QStringLiteral("of 1")) ||
                                 result->text() == QStringLiteral("1 results"),
                             5000);

    QVERIFY(area.openFile(second));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(result->text().contains(QStringLiteral("of 3")) ||
                                 result->text() == QStringLiteral("3 results"),
                             5000);
    auto* tabs = area.findChild<QTabWidget*>();
    QVERIFY(tabs);
    tabs->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(result->text().contains(QStringLiteral("of 1")) ||
                                 result->text() == QStringLiteral("1 results"),
                             5000);
}

void EditorBehaviorTest::transientDeletionDoesNotOrphanTab() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("transient.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("original")));

    litecode::ui::EditorArea area;
    QSignalSpy opened(&area, &litecode::ui::EditorArea::documentOpened);
    QVERIFY(area.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    QVERIFY(QFile::remove(path));
    QVERIFY(writeBytes(path, QByteArrayLiteral("restored")));
    auto* tabs = area.findChild<QTabWidget*>();
    QVERIFY(tabs);
    QTest::qWait(250);
    QVERIFY(!tabs->tabBar()->tabData(0).toBool());
}

void EditorBehaviorTest::pathIdentityFollowsFileSystemCaseSensitivity() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString lower = directory.filePath(QStringLiteral("case-probe.txt"));
    const QString upper = directory.filePath(QStringLiteral("CASE-PROBE.TXT"));
    QVERIFY(writeBytes(lower, QByteArrayLiteral("lower")));
    if (litecode::core::fileSystemCaseSensitivity(directory.path()) == Qt::CaseSensitive) {
        QVERIFY(writeBytes(upper, QByteArrayLiteral("upper")));
        QVERIFY(!litecode::core::pathsReferToSameEntry(lower, upper));
    } else {
        QVERIFY(litecode::core::pathsReferToSameEntry(lower, upper));
    }
}

QTEST_MAIN(EditorBehaviorTest)
#include "tst_editor_behavior.moc"
