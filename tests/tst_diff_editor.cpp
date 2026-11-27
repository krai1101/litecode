#include "editor/DiffModel.h"
#include "editor/ScintillaEditor.h"
#include "ui/DiffEditorWidget.h"
#include "ui/EditorArea.h"
#include "ui/Theme.h"
#include "ui/components/WorkbenchTabs.h"
#include <QFile>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode;
class DiffEditorTests : public QObject {
    Q_OBJECT
  private slots:
    void addedBlankRowsHaveAContinuousBackgroundAcrossTheGutter() {
        workspace::GitComparison data;
        data.path = "example.py";
        data.modified = "\n\n\n";
        ui::DiffEditorWidget view(data);
        view.resize(600, 300);
        view.setDarkTheme(false);
        view.setInlineMode(true);
        view.show();
        QTRY_COMPARE(view.hunks().size(), 1);
        auto* control = view.findChild<editor::ScintillaEditor*>("diffInline");
        QVERIFY(control);
        QTest::qWait(30);
        const auto image = control->grab().toImage();
        const auto ratio = image.devicePixelRatio();
        const int y = qRound((control->diffLineY(1) + control->diffLineHeight() - 2) * ratio);
        const auto background = image.pixelColor(qRound(300 * ratio), y);
        QCOMPARE(image.pixelColor(qRound(10 * ratio), y), background);
        QCOMPARE(image.pixelColor(qRound(100 * ratio), y), background);
    }
    void pureAdditionsHaveNoIntralineHighlight() {
        const auto added = editor::makeInlineDiff({}, "first\nsecond\n", {{0, 0, 0, 2}});
        QCOMPARE(added.added.size(), 2);
        QVERIFY(added.addedSpans.isEmpty());
        const auto removed = editor::makeInlineDiff("first\nsecond\n", {}, {{0, 2, 0, 0}});
        QCOMPARE(removed.removed.size(), 2);
        QVERIFY(removed.removedSpans.isEmpty());
    }
    void compactInlineContext() {
        QByteArray original;
        for (int row = 0; row < 80; ++row)
            original += "line " + QByteArray::number(row) + '\n';
        auto modified = original;
        modified.replace("line 20\n", "changed 20\n");
        modified.replace("line 60\n", "changed 60\n");
        const auto display =
            editor::makeInlineDiff(original, modified, editor::compareLines(original, modified), 3);
        QCOMPARE(display.removed.size(), 2);
        QCOMPARE(display.added.size(), 2);
        QCOMPARE(display.changeRows.size(), 2);
        QVERIFY(display.text.contains("line 17\n"));
        QVERIFY(display.text.contains("line 23\n"));
        QVERIFY(!display.text.contains("line 16\n"));
        QVERIFY(!display.text.contains("line 40\n"));
        QVERIFY(display.text.contains("unchanged lines"));
        QVERIFY(display.labels.size() < 24);
        for (const auto& span : display.addedSpans)
            QVERIFY(display.text.mid(span.first, span.second).startsWith("changed"));
        QVERIFY(editor::makeInlineDiff(original, original, {}, 3).text.isEmpty());
    }
    void inlineRowsAndMode() {
        const QByteArray original("first\nold value\nlast\n"), modified("first\nnew value\nlast\n");
        const auto hunks = editor::compareLines(original, modified);
        const auto display = editor::makeInlineDiff(original, modified, hunks);
        QCOMPARE(display.text, QByteArray("first\nold value\nnew value\nlast\n"));
        QCOMPARE(display.removed, QVector<int>{1});
        QCOMPARE(display.added, QVector<int>{2});
        QVERIFY(display.labels[1].endsWith('-'));
        QVERIFY(display.labels[2].endsWith('+'));
        QVERIFY(!display.removedSpans.isEmpty());
        workspace::GitComparison comparison;
        comparison.path = "sample.cpp";
        comparison.original = original;
        comparison.modified = modified;
        ui::DiffEditorWidget widget(comparison);
        widget.resize(600, 400);
        widget.show();
        widget.setInlineMode(true);
        QTRY_COMPARE(widget.hunks().size(), 1);
        auto* control = widget.findChild<editor::ScintillaEditor*>("diffInline");
        QVERIFY(control && control->isVisible() && control->isReadOnly());
        QCOMPARE(control->text(), display.text);
        QVERIFY(!widget.findChild<editor::ScintillaEditor*>("diffOriginal")->isVisible());
        for (bool dark : {false, true}) {
            widget.setDarkTheme(dark);
            QCOMPARE(control->text(), display.text);
            widget.grab().save(
                QString("build/release/tests/inline-%1.png").arg(dark ? "dark" : "light"));
        }
        widget.setInlineMode(false);
        QVERIFY(widget.findChild<editor::ScintillaEditor*>("diffOriginal")->isVisible());
    }
    void lineComparisons() {
        QVERIFY(editor::compareLines("same\n", "same\n").isEmpty());
        auto hunks = editor::compareLines("a\nb\nc\n", "a\nB\nc\nd\n");
        QCOMPARE(hunks.size(), 2);
        QCOMPARE(hunks[0].originalStart, 1);
        QCOMPARE(hunks[0].originalCount, 1);
        QCOMPARE(hunks[0].modifiedCount, 1);
        QCOMPARE(hunks[1].originalCount, 0);
        QCOMPARE(hunks[1].modifiedCount, 1);
        auto newline = editor::compareLines("last", "last\n");
        QCOMPARE(newline.size(), 1);
        QCOMPARE(newline.first().originalStart, 0);
        QCOMPARE(newline.first().originalCount, 1);
        QCOMPARE(newline.first().modifiedCount, 1);
        // Apply all computed ranges; unchanged runs must remain identical.
        QRandomGenerator random(42);
        for (int test = 0; test < 200; ++test) {
            QByteArray a, b;
            for (int i = 0; i < 20; ++i) {
                a += QByteArray::number(random.bounded(8)) + '\n';
                b += QByteArray::number(random.bounded(8)) + '\n';
            }
            auto result = editor::compareLines(a, b);
            auto lines = a.split('\n');
            const auto modified = b.split('\n');
            for (auto it = result.crbegin(); it != result.crend(); ++it) {
                for (int i = 0; i < it->originalCount; ++i)
                    lines.removeAt(it->originalStart);
                for (int i = it->modifiedCount - 1; i >= 0; --i)
                    lines.insert(it->originalStart, modified[it->modifiedStart + i]);
            }
            QCOMPARE(lines.join('\n'), b);
        }
    }
    void nativePanesAndDocumentUndo() {
        workspace::GitComparison data;
        data.path = data.originalPath = "test.cpp";
        data.original = "one\ntwo\nthree\n";
        data.modified = "inserted\none\nchanged\nthree\n";
        ui::DiffEditorWidget view(data);
        editor::ScintillaEditor model;
        model.setText(data.modified);
        model.markSaved();
        view.bindWorkingEditor(&model);
        view.resize(900, 500);
        view.show();
        auto* original = view.findChild<editor::ScintillaEditor*>("diffOriginal");
        auto* modified = view.findChild<editor::ScintillaEditor*>("diffModified");
        QVERIFY(original && modified);
        QVERIFY(original->isReadOnly());
        QVERIFY(!modified->isReadOnly());
        QTRY_COMPARE(view.hunks().size(), 2);
        QTest::qWait(30);
        QCOMPARE(original->mapToGlobal(QPoint(0, original->diffLineY(1))).y(),
                 modified->mapToGlobal(QPoint(0, modified->diffLineY(2))).y());
        QCOMPARE(original->text(), data.original);
        QCOMPARE(model.text(), data.modified); // View padding cannot edit the model.
        modified->replaceBytes(0, 0, "extra\n");
        QCOMPARE(model.text(), QByteArray("extra\n") + data.modified);
        QVERIFY(model.isModified());
        view.undo();
        QCOMPARE(model.text(), data.modified);
        QCOMPARE(modified->text(), data.modified);
        view.redo();
        QCOMPARE(modified->text(), QByteArray("extra\n") + data.modified);
        QTRY_VERIFY(!view.hunks().isEmpty() && view.hunks().first().modifiedCount == 2);
        view.revertCurrentChange();
        QCOMPARE(model.text(), QByteArray("one\nchanged\nthree\n"));
        view.undo();
        QCOMPARE(model.text(), QByteArray("extra\n") + data.modified);
        QTRY_VERIFY(!view.hunks().isEmpty() && view.hunks().first().modifiedCount == 2);
        for (bool dark : {false, true}) {
            view.setDarkTheme(dark);
            QCOMPARE(original->text(), data.original);
            QCOMPARE(modified->text(), model.text());
            if (const auto capture = qEnvironmentVariable("LITECODE_DIFF_CAPTURE");
                !capture.isEmpty()) {
                QTest::qWait(50);
                view.grab().save(capture + (dark ? "-dark.png" : "-light.png"));
            }
        }
    }
    void alignedScrolling() {
        workspace::GitComparison data;
        data.path = data.originalPath = "long.cpp";
        for (int i = 0; i < 120; ++i) {
            const auto line = QByteArray("line ") + QByteArray::number(i) + '\n';
            data.original += line;
            if (i == 20)
                data.modified += "added 1\nadded 2\nadded 3\n";
            if (i != 80 && i != 81)
                data.modified += line;
        }
        ui::DiffEditorWidget view(data);
        view.resize(800, 300);
        view.show();
        QTRY_COMPARE(view.hunks().size(), 2);
        auto* original = view.findChild<editor::ScintillaEditor*>("diffOriginal");
        auto* modified = view.findChild<editor::ScintillaEditor*>("diffModified");
        auto* scroll = view.findChild<QScrollBar*>("diffVerticalScroll");
        QVERIFY(scroll && scroll->maximum() > 0);
        scroll->setValue(50);
        QTest::qWait(30);
        QCOMPARE(original->mapToGlobal(QPoint(0, original->diffLineY(60))).y(),
                 modified->mapToGlobal(QPoint(0, modified->diffLineY(63))).y());
        scroll->setValue(scroll->maximum());
        QTest::qWait(30);
        QCOMPARE(original->mapToGlobal(QPoint(0, original->diffLineY(119))).y(),
                 modified->mapToGlobal(QPoint(0, modified->diffLineY(120))).y());
        QCOMPARE(original->text(), data.original);
        QCOMPARE(modified->text(), data.modified);
    }
    void finalNewlineRevert() {
        workspace::GitComparison data;
        data.path = data.originalPath = "newline.txt";
        data.original = "last";
        data.modified = "last\n";
        ui::DiffEditorWidget view(data);
        editor::ScintillaEditor model;
        model.setText(data.modified);
        view.bindWorkingEditor(&model);
        QTRY_COMPARE(view.hunks().size(), 1);
        view.revertCurrentChange();
        QCOMPARE(model.text(), data.original);
        view.undo();
        QCOMPARE(model.text(), data.modified);
    }
    void workingDiffSavesThroughEditorArea() {
        QTemporaryDir directory;
        QFile file(directory.filePath("test.cpp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("new\n");
        file.close();
        workspace::GitComparison data;
        data.repository = directory.path();
        data.path = data.originalPath = "test.cpp";
        data.original = "old\n";
        data.modified = "new\n";
        ui::EditorArea area;
        area.openGitComparison(data);
        auto* view = area.findChild<ui::DiffEditorWidget*>("gitDiffEditor");
        QVERIFY(view);
        QTRY_VERIFY(view->workingEditor());
        auto* modified = view->findChild<editor::ScintillaEditor*>("diffModified");
        modified->replaceBytes(0, 3, "changed");
        QString diagnostic;
        QVERIFY2(area.saveCurrent(&diagnostic), qPrintable(diagnostic));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("changed\n"));
        file.close();
        QCOMPARE(
            area.findChild<litecode::ui::components::WorkbenchTabs*>("editorTabs")->currentWidget(),
            view);
        area.undo();
        QCOMPARE(modified->text(), QByteArray("new\n"));
        QVERIFY(area.documents().first().modified);
        // Staged snapshots never bind a writable document.
        data.staged = true;
        area.openGitComparison(data);
        auto* staged = qobject_cast<ui::DiffEditorWidget*>(
            area.findChild<litecode::ui::components::WorkbenchTabs*>("editorTabs")
                ->currentWidget());
        QVERIFY(staged && !staged->workingEditor());
        QVERIFY(staged->findChild<editor::ScintillaEditor*>("diffModified")->isReadOnly());
    }
};
QTEST_MAIN(DiffEditorTests)
#include "tst_diff_editor.moc"
