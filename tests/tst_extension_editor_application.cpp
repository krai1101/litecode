#include "editor/ScintillaEditor.h"
#include "extensions/EditorStateWire.h"
#include "extensions/ExtensionManager.h"
#include "extensions/TextEditorEditData.h"
#include "ui/EditorArea.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLayout>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace litecode;
namespace {
QString createFile(const QTemporaryDir& directory, const QByteArray& text) {
    const auto path = directory.filePath("edit.txt");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size())
        throw std::runtime_error("Cannot create editor fixture.");
    return path;
}
extensions::TextEditorEditData editFor(const ui::EditorArea& area, core::DocumentId id) {
    extensions::TextEditorEditData data;
    data.documentVersionId = area.extensionDocumentSnapshot(id)->version;
    return data;
}
} // namespace
class EditorApplicationTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeExtensionReadsEditorAndAppliesEditAcrossSeparateHost() {
        QTemporaryDir directory;
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        const auto path = createFile(directory, "abc");
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        widget->selectByteRange(1, 1);
        const auto editor = area.extensionEditorState().editors.first();
        const core::DocumentId id(editor.documentId.toULongLong());
        extensions::ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.documents().update(*area.extensionDocumentSnapshot(id));
        manager.setEditorStateSource(
            [&area] { return area.extensionEditorState(); },
            [&area](const QString& identity, const extensions::TextEditorEditData& edit) {
                return area.applyTextEditorEdit(identity, edit);
            });
        connect(&area, &ui::EditorArea::extensionEditorStateChanged, &manager,
                &extensions::ExtensionManager::setEditorState);
        connect(&area, &ui::EditorArea::documentEdited, &manager,
                [&area, &manager](const core::DocumentEdit& edit) {
                    const auto snapshot = area.extensionDocumentSnapshot(edit.documentId);
                    QVERIFY(snapshot);
                    const extensions::Range range(edit.range.start.line,
                                                  edit.range.start.utf16Column, edit.range.end.line,
                                                  edit.range.end.utf16Column);
                    if (!manager.documents().change(
                            QString::number(edit.documentId.value()), edit.version + 1, range,
                            QString::fromUtf8(edit.insertedText), snapshot->dirty))
                        manager.documents().update(*snapshot);
                });
        connect(&area, &ui::EditorArea::documentEditsApplied, &manager,
                [&area, &manager](core::DocumentId documentId,
                                  const QVector<core::DocumentEdit>& edits) {
                    const auto snapshot = area.extensionDocumentSnapshot(documentId);
                    QVERIFY(snapshot);
                    QVector<extensions::TextEdit> changes;
                    for (const auto& edit : edits)
                        changes.append(extensions::TextEdit(
                            extensions::Range(edit.range.start.line, edit.range.start.utf16Column,
                                              edit.range.end.line, edit.range.end.utf16Column),
                            QString::fromUtf8(edit.insertedText)));
                    if (!edits.isEmpty() &&
                        !manager.documents().changeBatch(QString::number(documentId.value()),
                                                         edits.first().version + 1, changes,
                                                         snapshot->dirty))
                        manager.documents().update(*snapshot);
                });
        connect(&area, &ui::EditorArea::documentSnapshotApplied, &manager,
                [&manager](const extensions::TextDocumentSnapshot& snapshot) {
                    manager.documents().update(snapshot);
                });
        connect(&area, &ui::EditorArea::documentSnapshotChanged, &manager,
                [&area, &manager](core::DocumentId documentId, core::DocumentEditReason) {
                    manager.documents().update(*area.extensionDocumentSnapshot(documentId));
                });
        manager.discover();
        QCOMPARE(manager.extensions().size(), 1);
        QVERIFY2(manager.extensions().first().error.isEmpty(),
                 qPrintable(manager.extensions().first().error));
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy replies(&manager, &extensions::ExtensionManager::commandResult);
        const auto read = manager.requestCommand("tests.native.editorContext");
        QVERIFY(read);
        QTRY_VERIFY_WITH_TIMEOUT(!replies.isEmpty(), 15000);
        auto reply = replies.takeFirst();
        QCOMPARE(reply[0].toULongLong(), read);
        QVERIFY2(reply[2].toString().isEmpty(), qPrintable(reply[2].toString()));
        auto context = reply[1].value<QJsonValue>().toObject();
        QCOMPARE(context.value("text").toString(), QString("abc"));
        QCOMPARE(context.value("active").toArray(), QJsonArray({0, 1}));
        const auto editRequest = manager.requestCommand("tests.native.editorEdit", {"X"});
        QVERIFY(editRequest);
        QTRY_VERIFY_WITH_TIMEOUT(!replies.isEmpty(), 15000);
        reply = replies.takeFirst();
        QCOMPARE(reply[0].toULongLong(), editRequest);
        QVERIFY2(reply[2].toString().isEmpty(), qPrintable(reply[2].toString()));
        const auto edit = reply[1].value<QJsonValue>().toObject();
        QVERIFY(edit.value("applied").toBool());
        QCOMPARE(edit.value("length").toInt(), 4);
        QCOMPARE(edit.value("active").toInt(), 2);
        QCOMPARE(widget->text(), QByteArray("aXbc"));
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("aXbc"));
        widget->undo();
        QCOMPARE(widget->text(), QByteArray("abc"));
        // The payload exceeds a frame and must use the fixed editor transfer
        // route, while the command request/result stay small.
        const int largeSize = 9 * 1024 * 1024;
        const auto largeRequest = manager.requestCommand("tests.native.editorEdit", {largeSize});
        QVERIFY(largeRequest);
        QVERIFY(!replies.isEmpty() || replies.wait(30000));
        reply = replies.takeFirst();
        QCOMPARE(reply[0].toULongLong(), largeRequest);
        QVERIFY2(reply[2].toString().isEmpty(), qPrintable(reply[2].toString()));
        QVERIFY(reply[1].value<QJsonValue>().toObject().value("applied").toBool());
        QCOMPARE(widget->text().size(), largeSize + 3);
        widget->undo();
        QCOMPARE(widget->text(), QByteArray("abc"));
        QVERIFY(manager.setEnabled("tests.native", false));
    }
    void editorStatePublicationFollowsDocumentEventsAndCoalescesSelectionChanges() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "abc");
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        QCoreApplication::processEvents();
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto initial = area.extensionEditorState().editors.first();
        const core::DocumentId id(initial.documentId.toULongLong());
        QStringList order;
        qint64 publishedVersion = initial.documentVersion;
        int editorEvents = 0;
        connect(&area, &ui::EditorArea::documentEditsApplied, &area,
                [&](core::DocumentId, const QVector<core::DocumentEdit>& edits) {
                    publishedVersion = edits.first().version + 1;
                    order.append("document");
                });
        connect(&area, &ui::EditorArea::extensionEditorStateChanged, &area,
                [&](const extensions::WindowEditorState& state) {
                    QCOMPARE(state.editors.first().documentVersion, publishedVersion);
                    ++editorEvents;
                    order.append("editor");
                });
        auto transaction = editFor(area, id);
        transaction.edits.append({extensions::Range(0, 0, 0, 0), QString("Q"), true});
        QVERIFY(area.applyTextEditorEdit(initial.id, transaction));
        QCOMPARE(order, QStringList{"document"});
        QTRY_COMPARE(editorEvents, 1);
        QCOMPARE(order, QStringList({"document", "editor"}));
        widget->selectByteRange(0, 0);
        widget->selectByteRange(1, 1);
        QTRY_COMPARE(editorEvents, 2);
        QVERIFY(area.extensionEditorState().editors.first().selections.first().active().isEqual(
            extensions::Position(0, 1)));
    }
    void editorContextUsesViewIdentityPrimaryFirstAndUtf16Columns() {
        QTemporaryDir directory;
        const auto path = createFile(directory, QString::fromUtf8("\xc3\xa9"
                                                                  "bc\nlast")
                                                    .toUtf8());
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        QVERIFY(widget->restoreExtensionSelections({{{4, 2}, {9, 9}}, 1}));
        const auto state = area.extensionEditorState();
        QCOMPARE(state.editors.size(), 1);
        QVERIFY(state.activeEditorId);
        const auto& editor = state.editors.first();
        QCOMPARE(*state.activeEditorId, editor.id);
        QVERIFY(editor.id != editor.documentId);
        QCOMPARE(editor.documentVersion,
                 area.extensionDocumentSnapshot(core::DocumentId(editor.documentId.toULongLong()))
                     ->version);
        QCOMPARE(editor.selections.size(), 2);
        QVERIFY(editor.selections[0].active().isEqual(extensions::Position(1, 4)));
        QVERIFY(editor.selections[1].anchor().isEqual(extensions::Position(0, 3)));
        QVERIFY(editor.selections[1].active().isEqual(extensions::Position(0, 1)));
        const auto decoded =
            extensions::wire::decodeEditorState(extensions::wire::encodeEditorState(state));
        QVERIFY(decoded);
        QVERIFY(extensions::wire::sameEditorState(state, *decoded));
        auto edit = editFor(area, core::DocumentId(editor.documentId.toULongLong()));
        edit.edits.append({extensions::Range(0, 0, 0, 0), QString("Q"), true});
        QVERIFY(area.applyTextEditorEdit(editor.id, edit));
        QCOMPARE(area.extensionEditorState().editors.first().id, editor.id);
        QVERIFY(!area.applyTextEditorEdit(QString("not-this-editor"), edit));
    }
    void auxiliaryViewClearsTextEditorContextAndRejectsRetainedIdentityEdits() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "abc");
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        const auto state = area.extensionEditorState();
        const auto editor = state.editors.first();
        auto edit = editFor(area, core::DocumentId(editor.documentId.toULongLong()));
        edit.edits.append({extensions::Range(0, 0, 0, 0), QString("X"), true});
        area.openAuxiliaryTab("fixture", "Auxiliary", new QWidget);
        QVERIFY(!area.extensionEditorState().activeEditorId);
        QVERIFY(area.extensionEditorState().editors.isEmpty());
        QVERIFY(!area.applyTextEditorEdit(editor.id, edit));
        QCOMPARE(
            area.extensionDocumentSnapshot(core::DocumentId(editor.documentId.toULongLong()))->text,
            QString("abc"));
    }
    void auxiliaryTabUsesTheFileTabCloseButtonLayout() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "abc");
        ui::EditorArea area;
        area.resize(700, 400);
        area.show();
        QVERIFY(area.openFile(path));
        auto* tabs = area.findChild<QTabBar*>("editorTabBar");
        QVERIFY(tabs && tabs->count() == 1);
        auto* fileActions = tabs->tabButton(0, QTabBar::RightSide);
        QVERIFY(fileActions && fileActions->layout());
        auto* fileClose = fileActions->findChild<QToolButton*>("editorTabClose");
        QTRY_VERIFY(fileClose && fileClose->isVisible());

        area.openAuxiliaryTab("extension:fixture", "Extension: Fixture", new QWidget);
        auto* detailsActions = tabs->tabButton(1, QTabBar::RightSide);
        QVERIFY(detailsActions && detailsActions->layout());
        auto* detailsClose = detailsActions->findChild<QToolButton*>("editorTabClose");
        QTRY_VERIFY(detailsClose && detailsClose->isVisible());
        QCOMPARE(detailsActions->size(), fileActions->size());
        QCOMPARE(detailsActions->layout()->contentsMargins(),
                 fileActions->layout()->contentsMargins());
        QCOMPARE(detailsClose->size(), fileClose->size());
        QCOMPARE(detailsClose->iconSize(), fileClose->iconSize());
        QVERIFY(detailsActions->layout()->contentsMargins().right() > 0);

        QTest::mouseClick(detailsClose, Qt::LeftButton);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(area.currentFile(), path);
    }
    void editorStateCodecRejectsInvalidCoordinatesVersionsAndActiveIdentity() {
        extensions::WindowEditorState state{
            {{"view",
              "1",
              2,
              {extensions::Selection(extensions::Position(0, 2), extensions::Position(0, 1))}}},
            "view"};
        auto value = extensions::wire::encodeEditorState(state);
        value.insert("activeEditorId", "unbound");
        QVERIFY(!extensions::wire::decodeEditorState(value));
        value = extensions::wire::encodeEditorState(state);
        auto editors = value.value("editors").toArray();
        auto editor = editors[0].toObject();
        editor.insert("documentVersion", "02");
        editors[0] = editor;
        value.insert("editors", editors);
        QVERIFY(!extensions::wire::decodeEditorState(value));
        editor.insert("documentVersion", "2");
        editor.insert("selections", QJsonArray{QJsonObject{{"anchor", QJsonArray{0, 1.5}},
                                                           {"active", QJsonArray{0, 0}}}});
        editors[0] = editor;
        value.insert("editors", editors);
        QVERIFY(!extensions::wire::decodeEditorState(value));
    }
    void eolPrecedesTextAndDefaultUndoStopWithOrderedNotifications() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "first\nlast");
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto model = area.documentModels().find(QUrl::fromLocalFile(path));
        const auto id = model->session().id();
        auto data = editFor(area, id);
        data.setEndOfLine = extensions::EndOfLine::CRLF;
        data.edits.append({extensions::Range(1, 0, 1, 4), QString("new"), false});
        QStringList events;
        connect(&area, &ui::EditorArea::documentSnapshotApplied, &area,
                [&](const extensions::TextDocumentSnapshot& snapshot) {
                    QCOMPARE(snapshot.text, QString("first\r\nlast"));
                    QCOMPARE(snapshot.version, data.documentVersionId + 1);
                    QCOMPARE(snapshot.eol, extensions::EndOfLine::CRLF);
                    events.append("eol");
                });
        connect(&area, &ui::EditorArea::documentEditsApplied, &area,
                [&](core::DocumentId, const QVector<core::DocumentEdit>& changes) {
                    QCOMPARE(changes.size(), 1);
                    events.append("text");
                });
        QString error;
        QVERIFY2(area.applyTextEditorEdit(id, data, &error), qPrintable(error));
        QCOMPARE(widget->text(), QByteArray("first\r\nnew"));
        QCOMPARE(model->contents(), widget->text());
        QCOMPARE(events, QStringList({"eol", "text"}));
        widget->undo();
        QCOMPARE(widget->text(), QByteArray("first\r\nlast"));
        QCOMPARE(model->contents(), widget->text());
        QCOMPARE(model->session().lineEnding(), core::LineEnding::CrLf);
        widget->undo();
        QCOMPARE(widget->text(), QByteArray("first\nlast"));
        QCOMPARE(model->contents(), widget->text());
        QCOMPARE(model->session().lineEnding(), core::LineEnding::Lf);
        widget->redo();
        widget->redo();
        QCOMPARE(model->contents(), QByteArray("first\r\nnew"));
        QCOMPARE(widget->text(), model->contents());
    }
    void absentStopsJoinLiveTransactionsWithoutSelectionRecoveryClosingGroup() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "abc");
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto model = area.documentModels().find(QUrl::fromLocalFile(path));
        const auto id = model->session().id();
        widget->selectByteRange(1, 1);
        auto first = editFor(area, id);
        first.undoStopAfter = false;
        first.edits.append({extensions::Range(0, 1, 0, 1), QString("X"), true});
        QVERIFY(area.applyTextEditorEdit(id, first));
        QCOMPARE(widget->selectionState().selections.first().active, qint64(2));
        auto second = editFor(area, id);
        second.undoStopBefore = false;
        second.edits.append({extensions::Range(0, 2, 0, 2), QString("Y"), true});
        QVERIFY(area.applyTextEditorEdit(id, second));
        QCOMPARE(widget->text(), QByteArray("aXYbc"));
        QCOMPARE(widget->selectionState().selections.first().active, qint64(3));
        widget->undo();
        QCOMPARE(widget->text(), QByteArray("abc"));
        QCOMPARE(model->contents(), widget->text());
        QCOMPARE(widget->selectionState().selections.first().active, qint64(1));
        widget->redo();
        QCOMPARE(widget->selectionState().selections.first().active, qint64(3));
    }
    void identicalReplacementMovesCursorWithoutDirtyingDocument() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "abc");
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto model = area.documentModels().find(QUrl::fromLocalFile(path));
        const auto id = model->session().id();
        widget->selectByteRange(1, 1);
        auto data = editFor(area, id);
        data.edits.append({extensions::Range(0, 0, 0, 2), QString("ab"), true});
        QSignalSpy changes(&area, &ui::EditorArea::documentEditsApplied);
        QVERIFY(area.applyTextEditorEdit(id, data));
        QCOMPARE(widget->selectionState().selections.first().active, qint64(2));
        QCOMPARE(area.extensionDocumentSnapshot(id)->version, data.documentVersionId);
        QVERIFY(!widget->isModified());
        QVERIFY(!model->session().isModified());
        QVERIFY(changes.isEmpty());
    }
    void multiSelectionsKeepDirectionAndPrimaryThroughUnicodeEdits() {
        QTemporaryDir directory;
        const auto path = createFile(directory, QString::fromUtf8("\xc3\xa9"
                                                                  "bc\nlast")
                                                    .toUtf8());
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto id = area.documentModels().find(QUrl::fromLocalFile(path))->session().id();
        QVERIFY(widget->restoreExtensionSelections({{{4, 2}, {9, 9}}, 1}));
        auto data = editFor(area, id);
        data.edits.append({extensions::Range(0, 0, 0, 0), QString("Q"), true});
        data.edits.append({extensions::Range(1, 4, 1, 4), QString("!"), true});
        QVERIFY(area.applyTextEditorEdit(id, data));
        const auto selections = widget->selectionState();
        QCOMPARE(selections.selections.size(), 2);
        QCOMPARE(selections.mainSelection, 1);
        QCOMPARE(selections.selections[0].anchor, qint64(5));
        QCOMPARE(selections.selections[0].active, qint64(3));
        QCOMPARE(selections.selections[1].active, qint64(11));
        widget->undo();
        const auto undone = widget->selectionState();
        QCOMPARE(undone.selections.size(), 2);
        QCOMPARE(undone.mainSelection, 1);
        QCOMPARE(undone.selections[0].anchor, qint64(4));
        QCOMPARE(undone.selections[0].active, qint64(2));
        QCOMPARE(undone.selections[1].active, qint64(9));
        widget->redo();
        const auto redone = widget->selectionState();
        QCOMPARE(redone.selections.size(), 2);
        QCOMPARE(redone.mainSelection, 1);
        QCOMPARE(redone.selections[0].anchor, qint64(5));
        QCOMPARE(redone.selections[0].active, qint64(3));
        QCOMPARE(redone.selections[1].active, qint64(11));
    }
    void staleReadOnlyAndInvalidPlansLeaveLiveEditorUntouched() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "abc\nlast");
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto id = area.documentModels().find(QUrl::fromLocalFile(path))->session().id();
        auto data = editFor(area, id);
        data.setEndOfLine = extensions::EndOfLine::CRLF;
        data.edits.append({extensions::Range(0, 0, 0, 2), QString("X"), true});
        ++data.documentVersionId;
        QVERIFY(!area.applyTextEditorEdit(id, data));
        --data.documentVersionId;
        widget->setReadOnly(true);
        QVERIFY(!area.applyTextEditorEdit(id, data));
        widget->setReadOnly(false);
        data.edits.append({extensions::Range(0, 1, 0, 3), QString("Y"), true});
        QVERIFY(!area.applyTextEditorEdit(id, data));
        QCOMPARE(widget->text(), QByteArray("abc\nlast"));
        QVERIFY(!widget->isModified());
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("abc\nlast"));
        QVERIFY(!area.applyTextEditorEdit(core::DocumentId(9999), data));
    }
    void eolConversionPreservesUnicodeSelectionColumnsBeforeTrackingText() {
        QTemporaryDir directory;
        const auto path = createFile(directory, QString::fromUtf8("\xc3\xa9\n\xf0\x9f\x98\x80"
                                                                  "x")
                                                    .toUtf8());
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto id = area.documentModels().find(QUrl::fromLocalFile(path))->session().id();
        QVERIFY(widget->restoreExtensionSelections({{{7, 3}, {8, 8}}, 1}));
        auto transaction = editFor(area, id);
        transaction.setEndOfLine = extensions::EndOfLine::CRLF;
        transaction.edits.append({extensions::Range(1, 2, 1, 2), QString("!"), true});
        QVERIFY(area.applyTextEditorEdit(id, transaction));
        const auto state = widget->selectionState();
        QCOMPARE(state.selections.size(), 2);
        QCOMPARE(state.mainSelection, 1);
        QCOMPARE(state.selections[0].anchor, qint64(9));
        QCOMPARE(state.selections[0].active, qint64(4));
        QCOMPARE(state.selections[1].active, qint64(10));
    }
};
QTEST_MAIN(EditorApplicationTests)
#include "tst_extension_editor_application.moc"
