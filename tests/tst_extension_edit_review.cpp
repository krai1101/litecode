#include "editor/ScintillaEditor.h"
#include "extensions/ExtensionManager.h"
#include "ui/DiffEditorWidget.h"
#include "ui/EditorArea.h"
#include "ui/extensions/WorkbenchDocumentOpenService.h"
#include "ui/extensions/WorkbenchWorkspaceEditService.h"
#include "ui/extensions/WorkspaceEditReviewService.h"
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace litecode;
namespace {
QString createFile(const QTemporaryDir& directory, const QString& name, const QByteArray& content) {
    const auto path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
        throw std::runtime_error("Cannot create review fixture.");
    return path;
}
QDialog* visibleReview(QWidget& window) {
    for (auto* dialog : window.findChildren<QDialog*>("workspaceEditReviewDialog"))
        if (dialog->isVisible())
            return dialog;
    return nullptr;
}
extensions::WorkspaceEditRequest edit(const QUrl& uri, const QString& text = "review ") {
    extensions::WorkspaceEdit changes;
    changes.insert(
        uri, extensions::Position(0, 0), text,
        extensions::WorkspaceEditEntryMetadata{true, "Proposed edit", "Please review", {}});
    return {extensions::detail::WorkspaceEditAccess::entries(changes), {}, {}};
}
} // namespace
class ExtensionEditReviewTests final : public QObject {
    Q_OBJECT
  private slots:
    void validNoopWorkspaceEditSucceedsWithoutMutation() {
        QTemporaryDir directory;
        const auto uri = QUrl::fromLocalFile(createFile(directory, "noop.txt", "body"));
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        ui::WorkbenchWorkspaceEditService edits(&area, &opens);
        auto opened = opens.open(uri, {}, "fixture");
        QTRY_VERIFY(opened.isFinished());
        const auto model = area.documentModels().find(uri);
        QVERIFY(model);
        const auto version = model->session().version();
        QSignalSpy changed(&area, &ui::EditorArea::documentEditsApplied);
        extensions::WorkspaceEdit changes;
        changes.replace(uri, extensions::Range(0, 0, 0, 4), "body");
        auto applied = edits.apply(
            "fixture", {extensions::detail::WorkspaceEditAccess::entries(changes), {}, {}});
        QTRY_VERIFY(applied.isFinished());
        QVERIFY(applied.result());
        QCOMPARE(model->contents(), QByteArray("body"));
        QCOMPARE(model->session().version(), version);
        QVERIFY(!model->session().isModified());
        QVERIFY(changed.isEmpty());
    }
    void nativeExtensionWaitsForRealPreviewApprovalAndPreservesUndo() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = createFile(directory, "review.txt", "body");
        const auto uri = QUrl::fromLocalFile(path);
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        QWidget window;
        window.resize(1100, 800);
        window.show();
        ui::EditorArea area(&window);
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        ui::WorkbenchDocumentOpenService opens(&area);
        ui::WorkspaceEditReviewService reviews(&window);
        ui::WorkbenchWorkspaceEditService edits(&area, &opens);
        edits.setReviewService(&reviews);
        extensions::ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.documents().setApplyEditDelegate(
            [&edits](const QString& owner, const extensions::WorkspaceEditRequest& request) {
                return edits.apply(owner, request);
            },
            [&edits](const QString& owner) { edits.removeOwner(owner); });
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy replies(&manager, &extensions::ExtensionManager::commandResult);
        const auto request = manager.requestCommand(
            "tests.native.applyEdit",
            QJsonArray{QJsonObject{
                {"uri", uri.toString()}, {"text", "prefix "}, {"needsConfirmation", true}}});
        QVERIFY(request);
        QTRY_VERIFY(visibleReview(window));
        QVERIFY(replies.isEmpty());
        QCOMPARE(widget->text(), QByteArray("body"));
        auto* preview = visibleReview(window)->findChild<ui::DiffEditorWidget*>();
        QVERIFY(preview);
        QVERIFY(!preview->workingEditor()); // A preview never binds a live editor.
        bool proposedTextVisible = false;
        for (auto* editor : preview->findChildren<editor::ScintillaEditor*>()) {
            QVERIFY(editor->isReadOnly());
            proposedTextVisible |= editor->text() == QByteArray("prefix body");
        }
        QVERIFY(proposedTextVisible);
        auto* apply = visibleReview(window)->findChild<QPushButton*>("workspaceEditReviewApply");
        QVERIFY(apply);
        QTest::mouseClick(apply, Qt::LeftButton);
        QTRY_COMPARE(replies.size(), 1);
        QCOMPARE(replies.first()[0].toULongLong(), request);
        QVERIFY(replies.first()[2].toString().isEmpty());
        QVERIFY(replies.first()[1].value<QJsonValue>().toBool());
        QCOMPARE(widget->text(), QByteArray("prefix body"));
        widget->undo();
        QCOMPARE(widget->text(), QByteArray("body"));
        widget->redo();
        QCOMPARE(widget->text(), QByteArray("prefix body"));
        replies.clear();
        const auto pending = manager.requestCommand(
            "tests.native.applyEdit", QJsonArray{QJsonObject{{"uri", uri.toString()},
                                                             {"text", "must not apply "},
                                                             {"needsConfirmation", true}}});
        QVERIFY(pending);
        QTRY_VERIFY(visibleReview(window));
        QVERIFY(replies.isEmpty());
        QVERIFY(manager.setEnabled("tests.native", false));
        QTRY_VERIFY(!visibleReview(window));
        QTRY_COMPARE(replies.size(), 1);
        QCOMPARE(replies.first()[0].toULongLong(), pending);
        QVERIFY(!replies.first()[2].toString().isEmpty());
        QCOMPARE(widget->text(), QByteArray("prefix body"));
    }
    void rejectedAndStalePreviewsNeverApplyUnseenChanges() {
        QTemporaryDir directory;
        const auto path = createFile(directory, "stale.txt", "original");
        const auto uri = QUrl::fromLocalFile(path);
        QWidget window;
        window.resize(1000, 750);
        window.show();
        ui::EditorArea area(&window);
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        ui::WorkbenchDocumentOpenService opens(&area);
        ui::WorkspaceEditReviewService reviews(&window);
        ui::WorkbenchWorkspaceEditService edits(&area, &opens);
        edits.setReviewService(&reviews);
        auto rejected = edits.apply("fixture", edit(uri));
        QTRY_VERIFY(visibleReview(window));
        visibleReview(window)->reject();
        QTRY_VERIFY(rejected.isFinished());
        QVERIFY(!rejected.result());
        QCOMPARE(widget->text(), QByteArray("original"));
        auto stale = edits.apply("fixture", edit(uri));
        QTRY_VERIFY(visibleReview(window));
        widget->replaceBytes(0, 0, "user ");
        visibleReview(window)->accept();
        QTRY_VERIFY(stale.isFinished());
        QVERIFY(!stale.result());
        QCOMPARE(widget->text(), QByteArray("user original"));
    }
    void ownerLossCancelsActiveAndQueuedReviews() {
        QTemporaryDir directory;
        const auto uri = QUrl::fromLocalFile(createFile(directory, "owners.txt", "body"));
        QWidget window;
        window.resize(1000, 750);
        window.show();
        ui::EditorArea area(&window);
        ui::WorkbenchDocumentOpenService opens(&area);
        ui::WorkspaceEditReviewService reviews(&window);
        ui::WorkbenchWorkspaceEditService edits(&area, &opens);
        edits.setReviewService(&reviews);
        auto first = edits.apply("first", edit(uri, "first "));
        QTRY_VERIFY(visibleReview(window));
        auto second = edits.apply("second", edit(uri, "second "));
        QTest::qWait(30);
        QVERIFY(!first.isFinished());
        QVERIFY(!second.isFinished());
        edits.removeOwner("second");
        QTRY_VERIFY(second.isFinished());
        QVERIFY(!second.result());
        QVERIFY(visibleReview(window));
        edits.removeOwner("first");
        QTRY_VERIFY(first.isFinished());
        QVERIFY(!first.result());
        QTRY_VERIFY(!visibleReview(window));
        // Unopened clean documents may be released with the last operation.
        if (const auto model = area.documentModels().find(uri))
            QCOMPARE(model->contents(), QByteArray("body"));
        QFile disk(uri.toLocalFile());
        QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), QByteArray("body"));
    }
    void reviewShutdownSettlesPendingEdit_data() {
        QTest::addColumn<bool>("closeWindow");
        QTest::newRow("service-disposed") << false;
        QTest::newRow("window-closed") << true;
    }
    void reviewShutdownSettlesPendingEdit() {
        QFETCH(bool, closeWindow);
        QTemporaryDir directory;
        const auto uri = QUrl::fromLocalFile(createFile(directory, "shutdown.txt", "body"));
        auto window = std::make_unique<QWidget>();
        window->resize(1000, 750);
        window->show();
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto reviews = std::make_unique<ui::WorkspaceEditReviewService>(window.get());
        ui::WorkbenchWorkspaceEditService edits(&area, &opens);
        edits.setReviewService(reviews.get());
        auto pending = edits.apply("fixture", edit(uri));
        QTRY_VERIFY(visibleReview(*window));
        if (closeWindow)
            window.reset();
        else
            reviews.reset();
        QTRY_VERIFY(pending.isFinished());
        QVERIFY(!pending.result());
        if (window)
            QTRY_VERIFY(!visibleReview(*window));
        QFile disk(uri.toLocalFile());
        QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), QByteArray("body"));
    }
};
QTEST_MAIN(ExtensionEditReviewTests)
#include "tst_extension_edit_review.moc"
