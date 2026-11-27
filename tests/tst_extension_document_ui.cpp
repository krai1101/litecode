#include "ui/EditorArea.h"
#include "editor/ScintillaEditor.h"
#include "ui/extensions/WorkbenchDocumentOpenService.h"
#include "ui/extensions/VirtualDocumentUpdates.h"
#include "ui/extensions/WorkbenchWorkspaceEditService.h"
#include "extensions/ExtensionManager.h"
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>

using namespace litecode;
class ExtensionDocumentUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void workspaceEolEditKeepsUndoRedoMetadataAndSavedBytesConsistent() {
        QTemporaryDir directory;
        const auto path = directory.filePath("eol-undo.txt");
        const auto uri = QUrl::fromLocalFile(path);
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("first\nlast"), qint64(10)); }
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto model = area.documentModels().find(uri);
        QVERIFY(model);
        const auto id = model->session().id();
        ui::WorkbenchDocumentOpenService opens(&area);
        ui::WorkbenchWorkspaceEditService service(&area, &opens);
        extensions::WorkspaceEdit edit;
        edit.set(uri, QVector<extensions::TextEdit>{
            extensions::TextEdit::insert(extensions::Position(0, 0), "prefix\n"),
            extensions::TextEdit::setEndOfLine(extensions::EndOfLine::LF),
            extensions::TextEdit::setEndOfLine(extensions::EndOfLine::CRLF)});
        auto applied = service.apply("fixture", {extensions::detail::WorkspaceEditAccess::entries(edit), {}, {}});
        QTRY_VERIFY(applied.isFinished());
        QVERIFY(applied.result());
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("prefix\r\nfirst\r\nlast"));
        QCOMPARE(model->session().lineEnding(), core::LineEnding::CrLf);
        core::DocumentEditReason lastReason = core::DocumentEditReason::Other;
        connect(&area, &ui::EditorArea::documentSnapshotChanged, &area,
            [&](core::DocumentId, core::DocumentEditReason reason) { lastReason = reason; });
        widget->undo();
        QCOMPARE(model->contents(), QByteArray("first\nlast"));
        QCOMPARE(model->session().lineEnding(), core::LineEnding::Lf);
        QCOMPARE(lastReason, core::DocumentEditReason::Undo);
        widget->redo();
        QCOMPARE(model->contents(), QByteArray("prefix\r\nfirst\r\nlast"));
        QCOMPARE(model->session().lineEnding(), core::LineEnding::CrLf);
        QCOMPARE(lastReason, core::DocumentEditReason::Redo);
        QString error;
        QVERIFY(area.saveDocument(id, &error));
        { QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("prefix\r\nfirst\r\nlast")); }
        widget->undo();
        QVERIFY(area.saveDocument(id, &error));
        { QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("first\nlast")); }
    }
    void workspaceEditServiceRejectsStaleVersionsConfirmationAndStoppedOwner() {
        QTemporaryDir directory;
        const auto path = directory.filePath("rpc-edit.txt");
        const auto uri = QUrl::fromLocalFile(path);
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("text"), qint64(4)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        ui::WorkbenchWorkspaceEditService edits(&area, &opens);
        auto opened = opens.open(uri, {}, "fixture");
        QTRY_VERIFY(opened.isFinished());
        const core::DocumentId id(opened.result().id.toULongLong());
        extensions::WorkspaceEdit value;
        value.insert(uri, extensions::Position(0, 0), "prefix ");
        extensions::WorkspaceEditRequest request{extensions::detail::WorkspaceEditAccess::entries(value),
            {{uri.toString(QUrl::FullyEncoded), opened.result().version + 1}}, {}};
        auto stale = edits.apply("fixture", request);
        QTRY_VERIFY(stale.isFinished());
        QVERIFY(!stale.result());
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("text"));
        request.versions.clear();
        auto pending = edits.apply("stopped", request);
        edits.removeOwner("stopped");
        QVERIFY(pending.isFinished());
        QVERIFY(!pending.result());
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("text"));
        extensions::WorkspaceEdit review;
        review.insert(uri, extensions::Position(0, 0), "review ", extensions::WorkspaceEditEntryMetadata{true, "Review", {}, {}});
        auto required = edits.apply("fixture", {extensions::detail::WorkspaceEditAccess::entries(review), {}, {}});
        QVERIFY(required.isFinished());
        QVERIFY(!required.result());
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("text"));
    }
    void visibleEditBatchKeepsInsertionOrderAndOneUndoGroup() {
        QTemporaryDir directory;
        const auto path = directory.filePath("batch-undo.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("body"), qint64(4)); }
        ui::EditorArea area;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        const auto model = area.documentModels().find(QUrl::fromLocalFile(path));
        QVERIFY(model);
        const auto id = model->session().id();
        core::DocumentEdit first, second;
        first.insertedText = "A";
        second.insertedText = "B";
        const auto version = model->session().version();
        QVERIFY(area.applyDocumentEditPlans({{id, version, {first, second}}}));
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("ABbody"));
        QCOMPARE(model->session().version(), version + 1);
        widget->undo();
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("body"));
        widget->redo();
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("ABbody"));
    }
    void multipleDocumentEditsValidateBeforeAnyMutation() {
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto first = opens.create({QString("plaintext"), QString("first"), {}}, "edit-owner");
        auto second = opens.create({QString("plaintext"), QString("second"), {}}, "edit-owner");
        QTRY_VERIFY(first.isFinished()); QTRY_VERIFY(second.isFinished());
        const core::DocumentId firstId(first.result().id.toULongLong()), secondId(second.result().id.toULongLong());
        const auto firstModel = area.documentModels().find(firstId), secondModel = area.documentModels().find(secondId);
        core::DocumentEdit one, two;
        one.removedLength = 5; one.insertedText = "FIRST";
        two.removedLength = 6; two.insertedText = "SECOND";
        const auto firstVersion = firstModel->session().version(), secondVersion = secondModel->session().version();
        QString error;
        QVERIFY(!area.applyDocumentEditPlans({{firstId, firstVersion, {one}}, {secondId, secondVersion + 1, {two}}}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(firstModel->contents(), QByteArray("first"));
        QCOMPARE(secondModel->contents(), QByteArray("second"));
        QCOMPARE(firstModel->session().version(), firstVersion);
        int notifications = 0;
        connect(&area, &ui::EditorArea::documentEditsApplied, &area, [&](core::DocumentId, const QVector<core::DocumentEdit>&) {
            ++notifications;
            QCOMPARE(firstModel->contents(), QByteArray("FIRST"));
            QCOMPARE(secondModel->contents(), QByteArray("SECOND"));
        });
        QVERIFY(area.applyDocumentEditPlans({{firstId, firstVersion, {one}}, {secondId, secondVersion, {two}}}, &error));
        QCOMPARE(notifications, 2);
        QCOMPARE(firstModel->session().version(), firstVersion + 1);
        QCOMPARE(secondModel->session().version(), secondVersion + 1);
    }
    void detachedFileTracksAtomicReplacementAndDeleteRecreate() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("watched.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("before"), qint64(6)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto opened = opens.open(QUrl::fromLocalFile(path), {}, "watch-owner");
        QTRY_VERIFY(opened.isFinished());
        const core::DocumentId id(opened.result().id.toULongLong());
        const auto initialVersion = opened.result().version;
        QSignalSpy closed(&area, &ui::EditorArea::documentClosed);
        QVERIFY(!area.hasDocumentEditor(id));
        { QSaveFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
          QCOMPARE(file.write("replacement"), qint64(11)); QVERIFY(file.commit()); }
        QTRY_COMPARE(area.extensionDocumentSnapshot(id)->text, QString("replacement"));
        QVERIFY(area.extensionDocumentSnapshot(id)->version > initialVersion);
        QVERIFY(!area.extensionDocumentSnapshot(id)->dirty);
        QVERIFY(QFile::remove(path));
        QTRY_VERIFY(area.documentModels().find(id)->session().isDeleted());
        QVERIFY(area.extensionDocumentSnapshot(id));
        QCOMPARE(closed.size(), 0);
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("restored"), qint64(8)); }
        QTRY_COMPARE(area.extensionDocumentSnapshot(id)->text, QString("restored"));
        QVERIFY(!area.documentModels().find(id)->session().isDeleted());
        QCOMPARE(closed.size(), 0);
        opens.removeOwner("watch-owner");
        QCOMPARE(closed.size(), 1);
    }
    void detachedDirtyContentsSurviveExternalDeleteAndRecreate() {
        QTemporaryDir directory;
        const auto path = directory.filePath("dirty-watch.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("disk"), qint64(4)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto opened = opens.open(QUrl::fromLocalFile(path), {}, "watch-owner");
        QTRY_VERIFY(opened.isFinished());
        const core::DocumentId id(opened.result().id.toULongLong());
        const auto model = area.documentModels().find(id);
        core::DocumentEdit edit;
        edit.documentId = id;
        edit.removedLength = 4;
        edit.insertedText = "unsaved";
        QVERIFY(model->applyEdit(&edit));
        const auto version = model->session().version();
        QVERIFY(QFile::remove(path));
        QTRY_VERIFY(model->session().isDeleted());
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("external"), qint64(8)); }
        QTRY_VERIFY(!model->session().isDeleted());
        QCOMPARE(model->contents(), QByteArray("unsaved"));
        QCOMPARE(model->session().version(), version);
        QVERIFY(model->session().isModified());
        opens.removeOwner("watch-owner");
        QVERIFY(area.documentModels().find(id));
    }
    void nativeProviderOpensAndUpdatesReadOnlyDocumentAcrossHostBoundary() {
        QTemporaryDir directory;
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ui::EditorArea area;
        extensions::ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        ui::WorkbenchDocumentOpenService opens(&area);
        opens.setContentProviders(&manager.contentProviders());
        ui::WorkbenchWorkspaceEditService workspaceEdits(&area, &opens);
        const QPointer<ui::WorkbenchWorkspaceEditService> editService(&workspaceEdits);
        manager.documents().setApplyEditDelegate([editService](const QString& owner, const extensions::WorkspaceEditRequest& request) {
            if (!editService) throw std::runtime_error("Workspace edit service stopped.");
            return editService->apply(owner, request);
        }, [editService](const QString& owner) { if (editService) editService->removeOwner(owner); });
        ui::VirtualDocumentUpdates updates(&area, &manager.contentProviders());
        const QPointer<ui::WorkbenchDocumentOpenService> service(&opens);
        manager.documents().setOpenDelegate(
            [service](const QString& owner, const QUrl& uri, const extensions::TextDocumentOpenOptions& options) {
                if (!service) throw std::runtime_error("Opening service stopped.");
                return service->open(uri, options, owner);
            }, [service](const QString& owner, const extensions::UntitledTextDocumentOptions& options) {
                if (!service) throw std::runtime_error("Opening service stopped.");
                return service->create(options, owner);
            }, [service](const QString& owner) { if (service) service->removeOwner(owner); });
        connect(&area, &ui::EditorArea::documentOpened, &manager, [&area, &manager](core::DocumentId id) {
            if (const auto snapshot = area.extensionDocumentSnapshot(id)) manager.documents().update(*snapshot);
        });
        connect(&area, &ui::EditorArea::documentEdited, &manager, [&area, &manager](const core::DocumentEdit& edit) {
            const auto metadata = area.extensionDocumentMetadata(edit.documentId);
            if (!metadata) return;
            const extensions::Range range(edit.range.start.line, edit.range.start.utf16Column,
                edit.range.end.line, edit.range.end.utf16Column);
            QVERIFY(manager.documents().change(QString::number(edit.documentId.value()), edit.version + 1,
                range, QString::fromUtf8(edit.insertedText), metadata->dirty));
        });
        connect(&area, &ui::EditorArea::documentClosed, &manager,
            [&manager](core::DocumentId id) { manager.documents().close(QString::number(id.value())); });
        QVector<core::DocumentEdit> lastBatch;
        connect(&area, &ui::EditorArea::documentEditsApplied, &manager,
            [&area, &manager, &lastBatch](core::DocumentId id, const QVector<core::DocumentEdit>& edits) {
                lastBatch = edits;
                const auto metadata = area.extensionDocumentMetadata(id);
                QVERIFY(metadata);
                QVector<extensions::TextEdit> changes;
                for (const auto& edit : edits) changes.append(extensions::TextEdit(
                    extensions::Range(edit.range.start.line, edit.range.start.utf16Column,
                        edit.range.end.line, edit.range.end.utf16Column), QString::fromUtf8(edit.insertedText)));
                QVERIFY(manager.documents().changeBatch(QString::number(id.value()), edits.first().version + 1,
                    changes, metadata->dirty));
            });
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy results(&manager, &extensions::ExtensionManager::commandResult);
        const QUrl uri("tests-generated:/code.py");
        QVERIFY(manager.requestCommand("tests.native.openDocument", {uri.toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        auto result = results.takeFirst();
        QVERIFY2(result.at(2).toString().isEmpty(), qPrintable(result.at(2).toString()));
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("text").toString(), QString("provider original"));
        QVERIFY(result.at(1).value<QJsonValue>().toObject().value("sameInstance").toBool());
        const auto model = area.documentModels().find(uri);
        QVERIFY(model);
        const auto id = model->session().id();
        QVERIFY(model->session().isReadOnly());
        QVERIFY(!model->session().isModified());
        core::DocumentEdit forbidden;
        forbidden.documentId = id;
        forbidden.insertedText = "user edit";
        QVERIFY(!model->applyEdit(&forbidden));
        QVERIFY(manager.requestCommand("tests.native.virtualChange", {"provider updated"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(results.takeFirst().at(2).toString().isEmpty());
        QTRY_COMPARE(area.extensionDocumentSnapshot(id)->text, QString("provider updated"));
        QVERIFY(!area.extensionDocumentSnapshot(id)->dirty);
        QVERIFY(manager.requestCommand("tests.native.openDocument", {uri.toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("text").toString(), QString("provider updated"));
        QVERIFY(result.at(1).value<QJsonValue>().toObject().value("sameInstance").toBool());
        QVERIFY(manager.requestCommand("tests.native.virtualChange", {"first\nstable\nlast"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(results.takeFirst().at(2).toString().isEmpty());
        // Provider updates are edits to an existing model, not setValue():
        // upstream normalizes inserted text to that model's current EOL.
        const auto newline = model->session().lineEnding() == core::LineEnding::CrLf
            ? QString("\r\n") : QString("\n");
        const auto beforeBatch = QString("first") + newline + "stable" + newline + "last";
        const auto afterBatch = QString("FIRST") + newline + "stable" + newline + "LAST";
        QTRY_COMPARE(area.extensionDocumentSnapshot(id)->text, beforeBatch);
        const auto beforeBatchVersion = area.extensionDocumentSnapshot(id)->version;
        QVERIFY(manager.requestCommand("tests.native.virtualChange", {"FIRST\nstable\nLAST"}) != 0);
        QTRY_COMPARE(results.size(), 1);
        QVERIFY(results.takeFirst().at(2).toString().isEmpty());
        QTRY_COMPARE(area.extensionDocumentSnapshot(id)->text, afterBatch);
        QCOMPARE(lastBatch.size(), 2);
        QCOMPARE(area.extensionDocumentSnapshot(id)->version, beforeBatchVersion + 1);
        QVERIFY(manager.requestCommand("tests.native.openDocument", {uri.toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("text").toString(), afterBatch);
        const auto diskPath = directory.filePath("host-watched.txt");
        const auto diskUri = QUrl::fromLocalFile(diskPath);
        { QFile file(diskPath); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("disk original"), qint64(13)); }
        QVERIFY(manager.requestCommand("tests.native.openDocument", {diskUri.toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("text").toString(), QString("disk original"));
        const auto diskId = area.documentModels().find(diskUri)->session().id();
        { QSaveFile file(diskPath); QVERIFY(file.open(QIODevice::WriteOnly));
          QCOMPARE(file.write("disk changed"), qint64(12)); QVERIFY(file.commit()); }
        QTRY_COMPARE(area.extensionDocumentSnapshot(diskId)->text, QString("disk changed"));
        // The host's cached open must see the propagated reload, not silently
        // re-read the file or construct a second TextDocument object.
        QVERIFY(manager.requestCommand("tests.native.openDocument", {diskUri.toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("text").toString(), QString("disk changed"));
        QVERIFY(result.at(1).value<QJsonValue>().toObject().value("sameInstance").toBool());
        QVERIFY(manager.requestCommand("tests.native.applyEdit", {QJsonObject{{"uri", diskUri.toString()}, {"text", "prefix "}}}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QVERIFY(result.at(1).value<QJsonValue>().toBool());
        QCOMPARE(area.extensionDocumentSnapshot(diskId)->text, QString("prefix disk changed"));
        QVERIFY(area.extensionDocumentSnapshot(diskId)->dirty);
        QVERIFY(manager.requestCommand("tests.native.applyEdit", {QJsonObject{{"uri", diskUri.toString()}, {"eol", 2}}}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QVERIFY(result.at(1).value<QJsonValue>().toBool());
        QCOMPARE(area.extensionDocumentSnapshot(diskId)->eol, extensions::EndOfLine::CRLF);
        QVERIFY(manager.requestCommand("tests.native.openDocument", {diskUri.toString()}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QCOMPARE(result.at(1).value<QJsonValue>().toObject().value("eol").toInt(), 2);
        { QFile file(diskPath); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("disk changed")); }
        QVERIFY(manager.requestCommand("tests.native.applyEdit", {
            QJsonObject{{"uri", diskUri.toString()}, {"text", "must not apply "}},
            QJsonObject{{"uri", uri.toString()}, {"text", "read-only edit"}}}) != 0);
        QTRY_COMPARE(results.size(), 1);
        result = results.takeFirst();
        QVERIFY(result.at(2).toString().isEmpty());
        QVERIFY(!result.at(1).value<QJsonValue>().toBool());
        QCOMPARE(area.extensionDocumentSnapshot(diskId)->text, QString("prefix disk changed"));
        QVERIFY(manager.setEnabled("tests.native", false));
        QVERIFY(!area.documentModels().find(uri));
        QVERIFY(area.documentModels().find(diskUri)); // unsaved edits outlive their host
        QCOMPARE(area.documentModels().references(diskId), 0);
    }
    void closingLoadingTabTransfersPendingOpenToDetachedReader() {
        QTemporaryDir directory;
        const auto path = directory.filePath("transfer.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("transfer"), qint64(8)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        QVERIFY(area.openFile(path));
        const auto id = area.documents().first().documentId;
        auto opened = opens.open(QUrl::fromLocalFile(path), {}, "owner");
        QVERIFY(area.closeDocument(id));
        QVERIFY(area.openFiles().isEmpty());
        QTRY_VERIFY(opened.isFinished());
        QCOMPARE(opened.result().id, QString::number(id.value()));
        QCOMPARE(opened.result().text, QString("transfer"));
        opens.removeOwner("owner");
        QVERIFY(area.documents().isEmpty());
    }
    void referenceExpiryRetainsVisibleDocumentUntilTabCloses() {
        QTemporaryDir directory;
        const auto path = directory.filePath("retained.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("retained"), qint64(8)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area, nullptr, {200, 80 * 1024 * 1024, 50});
        auto opened = opens.open(QUrl::fromLocalFile(path), {}, "owner");
        QTRY_VERIFY(opened.isFinished());
        const auto id = core::DocumentId(opened.result().id.toULongLong());
        QVERIFY(area.openFile(path));
        QTRY_COMPARE(area.documentModels().references(id), 0);
        QVERIFY(area.extensionDocumentSnapshot(id));
        QVERIFY(area.closeDocument(id));
        QVERIFY(!area.extensionDocumentSnapshot(id));
    }
    void lastOwnerReleaseClosesCleanDetachedButNotUnsavedDocuments() {
        QTemporaryDir directory;
        const auto path = directory.filePath("owners.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("owners"), qint64(6)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto first = opens.open(QUrl::fromLocalFile(path), {}, "first");
        auto second = opens.open(QUrl::fromLocalFile(path), {}, "second");
        QTRY_VERIFY(first.isFinished());
        QTRY_VERIFY(second.isFinished());
        const auto id = core::DocumentId(first.result().id.toULongLong());
        QCOMPARE(second.result().id, first.result().id);
        QCOMPARE(area.documentModels().references(id), 2);
        opens.removeOwner("first");
        QCOMPARE(area.documentModels().references(id), 1);
        QVERIFY(area.extensionDocumentSnapshot(id));
        opens.removeOwner("second");
        QVERIFY(!area.extensionDocumentSnapshot(id));
        auto untitled = opens.create({}, "first");
        QTRY_VERIFY(untitled.isFinished());
        const auto dirtyId = core::DocumentId(untitled.result().id.toULongLong());
        QVERIFY(untitled.result().dirty);
        opens.removeOwner("first");
        QVERIFY(area.extensionDocumentSnapshot(dirtyId));
        QCOMPARE(area.documentModels().references(dirtyId), 0);
        QVERIFY(area.extensionDocumentSnapshot(dirtyId)->dirty);
    }
    void ownerRemovalDuringLoadingDoesNotStealAnotherOwnersResult() {
        QTemporaryDir directory;
        const auto path = directory.filePath("pending-owner.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("pending"), qint64(7)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto stopped = opens.open(QUrl::fromLocalFile(path), {}, "stopped");
        auto active = opens.open(QUrl::fromLocalFile(path), {}, "active");
        opens.removeOwner("stopped");
        QVERIFY(stopped.isFinished());
        QVERIFY_EXCEPTION_THROWN(stopped.result(), std::exception);
        QTRY_VERIFY(active.isFinished());
        QCOMPARE(active.result().text, QString("pending"));
        opens.removeOwner("active");
        QVERIFY(area.documents().isEmpty());
    }
    void detachedOpenReusesModelWhenAttached() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("detached.py");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("value = 42\n"), qint64(11)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto first = opens.open(QUrl::fromLocalFile(path), {});
        auto second = opens.open(QUrl::fromLocalFile(path), {});
        QTRY_VERIFY(first.isFinished());
        QTRY_VERIFY(second.isFinished());
        const auto snapshot = first.result();
        QCOMPARE(snapshot.id, second.result().id);
        QCOMPARE(snapshot.text, QString("value = 42\n"));
        QVERIFY(area.openFiles().isEmpty());
        QCOMPARE(area.documents().size(), 1);
        QVERIFY(area.openFile(path));
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        QVERIFY(widget);
        QCOMPARE(QString::number(area.documents().first().documentId.value()), snapshot.id);
        widget->replaceBytes(8, 2, "99");
        const auto id = area.documents().first().documentId;
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("value = 99\n"));
        auto third = opens.open(QUrl::fromLocalFile(path), {});
        QTRY_VERIFY(third.isFinished());
        QVERIFY(third.result().dirty);
        QCOMPARE(third.result().text, QString("value = 99\n"));
        auto rejected = opens.open(QUrl::fromLocalFile(path), {QString("utf16le")});
        QTRY_VERIFY(rejected.isFinished());
        QVERIFY_EXCEPTION_THROWN(rejected.result(), std::exception);
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("value = 99\n"));
    }
    void untitledCreationAndDetachedSaveUseSameModel() {
        QTemporaryDir directory;
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto created = opens.create({QString("python"), QString("print(42)\n"), QString("utf8")});
        QTRY_VERIFY(created.isFinished());
        const auto snapshot = created.result();
        QCOMPARE(snapshot.uri.scheme(), QString("untitled"));
        QCOMPARE(snapshot.languageId, QString("python"));
        QVERIFY(snapshot.dirty);
        QVERIFY(area.openFiles().isEmpty());
        const auto id = area.documents().first().documentId;
        QString error;
        QVERIFY(!area.saveDocument(id, &error));
        QVERIFY(area.saveDocumentAs(id, directory.filePath("created.py"), &error));
        const auto saved = area.extensionDocumentSnapshot(id).value();
        QCOMPARE(saved.text, snapshot.text);
        QVERIFY(saved.uri.isLocalFile());
        QVERIFY(!saved.dirty);
        auto reopened = opens.open(saved.uri, {});
        QTRY_VERIFY(reopened.isFinished());
        QCOMPARE(reopened.result().id, snapshot.id);
        QVERIFY(area.closeDocument(id));
        QVERIFY(area.documents().isEmpty());
        QVERIFY(!area.extensionDocumentSnapshot(id));
    }
    void openWhileVisibleReaderIsPendingAndUnsupportedScheme() {
        QTemporaryDir directory;
        const auto path = directory.filePath("pending.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("pending"), qint64(7)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        QVERIFY(area.openFile(path));
        auto pending = opens.open(QUrl::fromLocalFile(path), {});
        QTRY_VERIFY(pending.isFinished());
        QCOMPARE(pending.result().text, QString("pending"));
        QCOMPARE(area.documents().size(), 1);
        auto unsupported = opens.open(QUrl("missing-provider:/document"), {});
        QTRY_VERIFY(unsupported.isFinished());
        QVERIFY_EXCEPTION_THROWN(unsupported.result(), std::exception);
    }
    void visibleAttachmentDuringDetachedReadDoesNotReplaceModel() {
        QTemporaryDir directory;
        const auto path = directory.filePath("race.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("original"), qint64(8)); }
        ui::EditorArea area;
        ui::WorkbenchDocumentOpenService opens(&area);
        auto opened = opens.open(QUrl::fromLocalFile(path), {});
        const auto id = area.documents().first().documentId;
        QVERIFY(area.openFile(path));
        QTRY_VERIFY(opened.isFinished());
        QCOMPARE(opened.result().id, QString::number(id.value()));
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>() != nullptr);
        auto* widget = area.findChild<editor::ScintillaEditor*>();
        widget->replaceBytes(0, 8, "edited");
        QCoreApplication::processEvents();
        QVERIFY(area.extensionDocumentSnapshot(id));
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString("edited"));
        QCOMPARE(area.documents().size(), 1);
    }
    void editorAndExtensionSnapshotShareEditsAndReloads() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("model.txt"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("alpha\nbeta\n"), qint64(11));
        }
        ui::EditorArea area;
        area.resize(600, 400);
        area.show();
        QString error;
        QVERIFY(area.openFile(path, &error));
        QCOMPARE(area.documents().size(), 1);
        const auto id = area.documents().first().documentId;
        QTRY_VERIFY(area.extensionDocumentSnapshot(id).has_value());
        auto initial = area.extensionDocumentSnapshot(id).value();
        const auto original = initial.text;
        auto* editorWidget = area.findChild<editor::ScintillaEditor*>();
        QVERIFY(editorWidget);
        editorWidget->replaceBytes(0, 5, QByteArrayLiteral("changed"));
        auto changed = area.extensionDocumentSnapshot(id).value();
        QCOMPARE(changed.text, QString::fromUtf8(editorWidget->text()));
        QVERIFY(changed.text.startsWith("changed"));
        QVERIFY(changed.version > initial.version);
        QVERIFY(changed.dirty);
        editorWidget->undo();
        auto undone = area.extensionDocumentSnapshot(id).value();
        QCOMPARE(undone.text, original);
        QCOMPARE(undone.text, QString::fromUtf8(editorWidget->text()));
        QVERIFY(undone.version > changed.version);
        editorWidget->redo();
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, changed.text);
        QVERIFY(area.saveDocument(id, &error));
        QVERIFY(!area.extensionDocumentSnapshot(id)->dirty);
        QSignalSpy closed(&area, &ui::EditorArea::documentClosed);
        QVERIFY(area.reloadDocument(id, &error));
        QCOMPARE(closed.size(), 0);
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, QString::fromUtf8(editorWidget->text()));
        QCOMPARE(area.extensionDocumentSnapshot(id)->text, changed.text);
        QVERIFY(area.closeDocument(id));
        QVERIFY(!area.extensionDocumentSnapshot(id));
    }
};
QTEST_MAIN(ExtensionDocumentUiTests)
#include "tst_extension_document_ui.moc"
