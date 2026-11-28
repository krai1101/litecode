#include "extensions/TextEditorEditWire.h"
#include "ui/extensions/EditorSelectionTracking.h"
#include "ui/extensions/TextEditorEditPreparation.h"
#include <QJsonArray>
#include <QtTest>
#include <limits>

using namespace litecode;
namespace {
editor::TextDocumentModel model(const QString& text) {
    editor::TextDocumentModel result(core::DocumentId(1), {});
    if (!result.initializeUntitled(text, core::TextEncoding::Utf8, core::LineEnding::Lf))
        throw std::runtime_error("Fixture initialization failed.");
    return result;
}
extensions::TextEditorEditData transaction(const editor::TextDocumentModel& source) {
    extensions::TextEditorEditData result;
    result.documentVersionId = source.session().version() + 1;
    return result;
}
} // namespace
class EditorEditPreparationTests final : public QObject {
    Q_OBJECT
  private slots:
    void trailingCarriageReturnMatchesCrLfModelValidation() {
        const auto source = model("abc\nlast");
        auto edit = transaction(source);
        edit.setEndOfLine = extensions::EndOfLine::CRLF;
        edit.edits.append({extensions::Range(0, 3, 0, 500), QString("X\r"), false});
        auto prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->replacement.contents(), QByteArray("abcX\r\nlast"));
        QCOMPARE(prepared->operations.first().insertedUtf16Length, qint64(1));
        edit.edits[0].range = extensions::Range(1, 4, 1, 4);
        prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->replacement.contents(), QByteArray("abc\r\nlastX"));
        edit.edits[0].range = extensions::Range(0, 1, 0, 1);
        prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->replacement.contents(), QByteArray("aX\r\nbc\r\nlast"));
        edit.setEndOfLine = extensions::EndOfLine::LF;
        edit.edits[0].range = extensions::Range(0, 3, 0, 3);
        prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->replacement.contents(), QByteArray("abcX\n\nlast"));
        edit.setEndOfLine = extensions::EndOfLine::CRLF;
        edit.edits[0].text = QString("X\r\n");
        prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->replacement.contents(), QByteArray("abcX\r\n\r\nlast"));
    }
    void stagesEolBeforeTextWithoutMutatingLiveModel() {
        const auto source = model("first\nlast");
        auto edit = transaction(source);
        edit.setEndOfLine = extensions::EndOfLine::CRLF;
        edit.undoStopBefore = false;
        edit.edits.append({extensions::Range(1, 0, 1, 4), QString("new\nline"), false});
        QString error;
        const auto prepared = ui::prepareTextEditorEdit(source, edit, &error);
        QVERIFY2(prepared, qPrintable(error));
        QCOMPARE(source.contents(), QByteArray("first\nlast"));
        QCOMPARE(source.session().lineEnding(), core::LineEnding::Lf);
        QVERIFY(prepared->eolEdit);
        QCOMPARE(prepared->replacement.contents(), QByteArray("first\r\nnew\r\nline"));
        QCOMPARE(prepared->replacement.session().lineEnding(), core::LineEnding::CrLf);
        QCOMPARE(prepared->operations.first().edit.position, qint64(7));
        QCOMPARE(prepared->operations.first().edit.insertedText, QByteArray("new\r\nline"));
        QVERIFY(!prepared->undoStopBefore && prepared->undoStopAfter);
        QCOMPARE(prepared->contentChanges.size(), 1);
    }
    void rejectsStaleReadOnlyAndOverlappingEdits() {
        const auto source = model("abc\ndef");
        auto edit = transaction(source);
        --edit.documentVersionId;
        QVERIFY(!ui::prepareTextEditorEdit(source, edit));
        edit = transaction(source);
        edit.setEndOfLine = extensions::EndOfLine::CRLF;
        edit.edits.append({extensions::Range(0, 0, 0, 3), QString("x"), false});
        edit.edits.append({extensions::Range(0, 1, 0, 2), QString("y"), false});
        QVERIFY(!ui::prepareTextEditorEdit(source, edit));
        QCOMPARE(source.contents(), QByteArray("abc\ndef"));
        editor::TextDocumentModel readonly(core::DocumentId(2), {});
        QVERIFY(readonly.initializeVirtual(QUrl("fixture:readonly"), "abc"));
        QVERIFY(!ui::prepareTextEditorEdit(readonly, transaction(readonly)));
    }
    void preservesCoincidentInsertionOrderAndMarkerFlags() {
        const auto source = model("abc");
        auto edit = transaction(source);
        edit.edits.append({extensions::Range(0, 1, 0, 1), QString("X"), false});
        edit.edits.append({extensions::Range(0, 1, 0, 1), QString("Y"), true});
        const auto prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->replacement.contents(), QByteArray("aXYbc"));
        QCOMPARE(prepared->operations[0].edit.insertedText, QByteArray("Y"));
        QVERIFY(prepared->operations[0].forceMoveMarkers);
        QCOMPARE(prepared->operations[1].edit.insertedText, QByteArray("X"));
        QVERIFY(!prepared->operations[1].forceMoveMarkers);
    }
    void adjustsSurrogatesAndConvertsUtf16ToUtf8() {
        const auto source = model(QString::fromUtf8("a\xf0\x9f\x98\x80"
                                                    "b\n\xc3\xa9"));
        auto edit = transaction(source);
        edit.edits.append({extensions::Range(0, 2, 0, 3), QString("X"), false});
        edit.edits.append({extensions::Range(1, 1, 1, 1), QString("Y"), true});
        const auto prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->replacement.contents(), QString::fromUtf8("aXb\n\xc3\xa9Y").toUtf8());
        QCOMPARE(prepared->operations[0].edit.position, qint64(9));
        QCOMPARE(prepared->operations[1].edit.position, qint64(1));
        QCOMPARE(prepared->operations[1].edit.removedLength, qint64(4));
        auto collapsed = transaction(source);
        collapsed.edits.append({extensions::Range(0, 2, 0, 2), QString("X"), true});
        const auto insertion = ui::prepareTextEditorEdit(source, collapsed);
        QVERIFY(insertion);
        QCOMPARE(insertion->replacement.contents(), QString::fromUtf8("aX\xf0\x9f\x98\x80"
                                                                      "b\n\xc3\xa9")
                                                        .toUtf8());
    }
    void retainsContentIdenticalOperationsAndRejectsBrokenUnicode() {
        const auto source = model("abc");
        auto edit = transaction(source);
        edit.edits.append({extensions::Range(0, 0, 0, 1), QString("a"), true});
        const auto prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        QCOMPARE(prepared->operations.size(), 1);
        QVERIFY(prepared->operations.first().forceMoveMarkers);
        QVERIFY(prepared->contentChanges.isEmpty());
        QCOMPARE(prepared->replacement.session().version(), source.session().version());
        edit.edits[0].text = QString(QChar(0xd800));
        QVERIFY(!ui::prepareTextEditorEdit(source, edit));
    }
    void rejectsOverlapIntroducedBySurrogateExpansionWithoutApplyingEol() {
        const auto source = model(QString::fromUtf8("a\xf0\x9f\x98\x80"
                                                    "b\nlast"));
        auto edit = transaction(source);
        edit.setEndOfLine = extensions::EndOfLine::CRLF;
        edit.edits.append({extensions::Range(0, 1, 0, 2), QString("left"), false});
        edit.edits.append({extensions::Range(0, 2, 0, 3), QString("right"), true});
        QVERIFY(!extensions::textEditorEditRangesOverlap(edit.edits));
        QVERIFY(!ui::prepareTextEditorEdit(source, edit));
        QCOMPARE(source.contents(), QString::fromUtf8("a\xf0\x9f\x98\x80"
                                                      "b\nlast")
                                        .toUtf8());
        QCOMPARE(source.session().lineEnding(), core::LineEnding::Lf);
    }
    void wirePreservesNullTextFlagsAndExactVersion() {
        extensions::TextEditorEditData data;
        data.documentVersionId = std::numeric_limits<qint64>::max();
        data.undoStopBefore = false;
        data.setEndOfLine = extensions::EndOfLine::CRLF;
        data.edits.append({extensions::Range(0, 0, 0, 1), std::nullopt, true});
        data.edits.append({extensions::Range(1, 0, 1, 1), QString{}, false});
        auto encoded = extensions::wire::encodeTextEditorEdit(data);
        const auto decoded = extensions::wire::decodeTextEditorEdit(encoded);
        QVERIFY(decoded);
        QCOMPARE(decoded->documentVersionId, data.documentVersionId);
        QVERIFY(!decoded->undoStopBefore && decoded->undoStopAfter);
        QCOMPARE(decoded->setEndOfLine, data.setEndOfLine);
        QVERIFY(!decoded->edits[0].text && decoded->edits[0].forceMoveMarkers);
        QVERIFY(decoded->edits[1].text.has_value() && !decoded->edits[1].forceMoveMarkers);
        encoded.insert("documentVersionId", "01");
        QVERIFY(!extensions::wire::decodeTextEditorEdit(encoded));
        encoded = extensions::wire::encodeTextEditorEdit(data);
        encoded.insert("undoStopBefore", 0);
        QVERIFY(!extensions::wire::decodeTextEditorEdit(encoded));
        encoded = extensions::wire::encodeTextEditorEdit(data);
        auto edits = encoded.value("edits").toArray();
        auto first = edits[0].toObject();
        first.insert("start", QJsonArray{0, 0.5});
        edits[0] = first;
        encoded.insert("edits", edits);
        QVERIFY(!extensions::wire::decodeTextEditorEdit(encoded));
    }
    void selectionTrackingUsesUtf16CommonLengthAndRetainsDirection() {
        const auto source = model(QString::fromUtf8("\xc3\xa9"
                                                    "bc"));
        auto edit = transaction(source);
        edit.edits.append({extensions::Range(0, 0, 0, 2), QString("XY"), false});
        auto prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        const auto retained = ui::trackEditorSelection({1, 1}, prepared->operations);
        QCOMPARE(retained.anchor, qint64(1));
        QCOMPARE(retained.active, qint64(1));
        edit.edits[0].forceMoveMarkers = true;
        prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        const auto forced = ui::trackEditorSelection({1, 1}, prepared->operations);
        QCOMPARE(forced.anchor, qint64(2));
        QCOMPARE(forced.active, qint64(2));
        const auto reversed = ui::trackEditorSelection({3, 0}, prepared->operations);
        QCOMPARE(reversed.anchor, qint64(3));
        QCOMPARE(reversed.active, qint64(2));
    }
    void selectionTrackingCollapsesCursorOnlyAfterTheCompleteTransaction() {
        const auto source = model("abc");
        auto edit = transaction(source);
        edit.edits.append({extensions::Range(0, 1, 0, 1), QString("X"), false});
        edit.edits.append({extensions::Range(0, 1, 0, 1), QString("Y"), false});
        const auto prepared = ui::prepareTextEditorEdit(source, edit);
        QVERIFY(prepared);
        const auto cursor = ui::trackEditorSelection({1, 1}, prepared->operations);
        QCOMPARE(cursor.anchor, qint64(3));
        QCOMPARE(cursor.active, qint64(3));
        const auto range = ui::trackEditorSelection({1, 2}, prepared->operations);
        QCOMPARE(range.anchor, qint64(1));
        QCOMPARE(range.active, qint64(4));
    }
};
QTEST_GUILESS_MAIN(EditorEditPreparationTests)
#include "tst_extension_editor_edit_preparation.moc"
