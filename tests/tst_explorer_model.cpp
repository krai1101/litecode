#include "core/FileSystemPath.h"
#include "editor/DocumentSession.h"
#include "ui/EditorArea.h"
#include "ui/controllers/EditorSessionController.h"
#include "ui/controllers/ExplorerController.h"
#include "ui/explorer/ExplorerFileSystemModel.h"
#include "ui/explorer/ExplorerTreeView.h"
#include "workspace/WorkspaceService.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <filesystem>

using litecode::ui::EditorArea;
using litecode::ui::EditorSessionController;
using litecode::ui::ExplorerController;
using litecode::ui::ExplorerFileSystemModel;
using litecode::ui::ExplorerTreeView;
using litecode::workspace::WorkspaceService;

class ExplorerModelTest final : public QObject {
    Q_OBJECT

  private slots:
    void temporaryFileIsARealChildAfterDirectories();
    void temporaryDirectoryIsFirstChild();
    void temporaryEntryCanBeNestedAndRemoved();
    void switchingInlineCreateKindKeepsTheNewEditor();
    void creatingExistingEntriesDoesNotOverwriteThem();
    void externalParentRemovalCancelsInlineCreate();
    void deletionUsesConfiguredTrashOperation();
    void trashDeleteRejectsReplacementAtConfirmedPath();
    void trashFailureRequiresExplicitPermanentDeletion();
    void permanentDeleteRejectsReplacementAtConfirmedPath();
    void permanentDeletionRemovesDirectoryLinkOnly();
    void externalDirectoryLinkIsManagedWithoutFollowingTarget();
    void caseOnlyRenameIsAllowedWithoutOverwritingAnotherEntry();
    void renameRejectsDifferentHardLinkName();
    void explorerRenameRebindsOpenDocument();
    void deletingPathKeepsAffectedDocumentsAsDeleted();
    void externalDeletionMarksOpenEditorAsDeleted();
    void deletedStateRemainsIndependentFromUnsavedEdits();
    void saveAsRejectsPathOpenInAnotherTab();
};

namespace {

void createFile(const QString& path) {
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
}

QModelIndex startLoadingRoot(ExplorerFileSystemModel& model, const QString& path) {
    model.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    return model.setRootPath(path);
}

} // namespace

void ExplorerModelTest::temporaryFileIsARealChildAfterDirectories() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    QVERIFY(QDir(workspace.path()).mkdir(QStringLiteral("src")));
    createFile(workspace.filePath(QStringLiteral("README.md")));

    ExplorerFileSystemModel model;
    const QModelIndex root = startLoadingRoot(model, workspace.path());
    QTRY_VERIFY_WITH_TIMEOUT(model.index(workspace.filePath(QStringLiteral("src"))).isValid(),
                             3000);
    QTRY_VERIFY_WITH_TIMEOUT(model.index(workspace.filePath(QStringLiteral("README.md"))).isValid(),
                             3000);
    const int previousCount = model.rowCount(root);
    const QModelIndex temporary = model.insertTemporaryEntry(root, false);

    QVERIFY(temporary.isValid());
    QCOMPARE(temporary.parent(), root);
    QCOMPARE(model.rowCount(root), previousCount + 1);
    QVERIFY(model.isTemporaryEntry(temporary));
    QVERIFY(!model.isDir(temporary));
    QVERIFY(!model.hasChildren(temporary));
    QVERIFY(temporary.row() > 0);
    QVERIFY(model.isDir(model.index(temporary.row() - 1, 0, root)));
    const QModelIndex readme = model.index(workspace.filePath(QStringLiteral("README.md")));
    QVERIFY(readme.isValid());
    QCOMPARE(readme.parent(), root);
    QCOMPARE(model.filePath(readme), workspace.filePath(QStringLiteral("README.md")));
}

void ExplorerModelTest::temporaryDirectoryIsFirstChild() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    createFile(workspace.filePath(QStringLiteral("README.md")));

    ExplorerFileSystemModel model;
    const QModelIndex root = startLoadingRoot(model, workspace.path());
    QTRY_VERIFY_WITH_TIMEOUT(model.rowCount(root) > 0, 3000);
    const QModelIndex temporary = model.insertTemporaryEntry(root, true);

    QCOMPARE(temporary.row(), 0);
    QCOMPARE(temporary.parent(), root);
    QVERIFY(model.isDir(temporary));
}

void ExplorerModelTest::temporaryEntryCanBeNestedAndRemoved() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    QVERIFY(QDir(workspace.path()).mkdir(QStringLiteral("empty")));

    ExplorerFileSystemModel model;
    const QModelIndex root = startLoadingRoot(model, workspace.path());
    QTRY_VERIFY_WITH_TIMEOUT(model.rowCount(root) > 0, 3000);
    const QModelIndex folder = model.index(workspace.filePath(QStringLiteral("empty")));
    QVERIFY(folder.isValid());
    if (model.canFetchMore(folder))
        model.fetchMore(folder);
    QTRY_VERIFY_WITH_TIMEOUT(!model.canFetchMore(folder), 3000);
    const int previousCount = model.rowCount(folder);

    const QModelIndex temporary = model.insertTemporaryEntry(folder, false);
    QCOMPARE(temporary.parent(), folder);
    QCOMPARE(model.rowCount(folder), previousCount + 1);
    model.removeTemporaryEntry();
    QCOMPARE(model.rowCount(folder), previousCount);
    QVERIFY(!model.temporaryEntry().isValid());
}

void ExplorerModelTest::switchingInlineCreateKindKeepsTheNewEditor() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString scripts = workspaceDirectory.filePath(QStringLiteral("scripts"));
    QVERIFY(QDir().mkpath(scripts));

    WorkspaceService workspace;
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    ExplorerController controller(workspace);
    ExplorerFileSystemModel model;
    const QModelIndex root = startLoadingRoot(model, workspaceDirectory.path());
    QTRY_VERIFY_WITH_TIMEOUT(model.index(scripts).isValid(), 3000);
    ExplorerTreeView tree(model);
    tree.setRootIndex(root);
    tree.resize(360, 240);
    tree.show();

    connect(&controller, &ExplorerController::editRequested, &tree,
            [&model, &tree](const litecode::ui::ExplorerEditRequest& request) {
                const QModelIndex parent = model.index(request.parentDirectory);
                tree.beginCreateEditor(parent, request.kind ==
                                                   litecode::ui::ExplorerEditKind::NewDirectory);
            });
    connect(&controller, &ExplorerController::editCancelled, &tree,
            &ExplorerTreeView::endInlineEditor);

    controller.setSelectedPath(scripts);
    controller.beginCreate(false);
    QTRY_VERIFY_WITH_TIMEOUT(tree.hasInlineEditor(), 3000);
    QVERIFY(!model.isDir(model.temporaryEntry()));
    QCOMPARE(model.temporaryEntry().parent(), model.index(scripts));

    // Selecting the synthetic row cannot resolve to a real filesystem path and clears the
    // ordinary selection, matching the toolbar sequence in MainWindow.
    controller.setSelectedPath({});
    controller.beginCreate(true);
    QTRY_VERIFY_WITH_TIMEOUT(tree.hasInlineEditor(), 3000);
    QVERIFY(model.temporaryEntry().isValid());
    QVERIFY(model.isDir(model.temporaryEntry()));
    QCOMPARE(model.temporaryEntry().parent(), model.index(scripts));
    QVERIFY(controller.activeEdit());
    QCOMPARE(controller.activeEdit()->kind, litecode::ui::ExplorerEditKind::NewDirectory);
    QCOMPARE(controller.activeEdit()->parentDirectory, QDir::toNativeSeparators(scripts));
    QVERIFY(controller.commitEdit(QStringLiteral("inside")));
    QVERIFY(QFileInfo(QDir(scripts).filePath(QStringLiteral("inside"))).isDir());
    QVERIFY(!QFileInfo::exists(workspaceDirectory.filePath(QStringLiteral("inside"))));
}

void ExplorerModelTest::creatingExistingEntriesDoesNotOverwriteThem() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString filePath = directory.filePath(QStringLiteral("existing.txt"));
    const QString folderPath = directory.filePath(QStringLiteral("existing-folder"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("keep this content"), qint64(17));
    file.close();
    QVERIFY(QDir().mkdir(folderPath));

    WorkspaceService workspace;
    QVERIFY(workspace.openFolder(directory.path()));
    ExplorerController controller(workspace);
    QSignalSpy created(&controller, &ExplorerController::entryCreated);
    QSignalSpy failed(&controller, &ExplorerController::operationFailed);

    controller.beginCreate(false, directory.path());
    QVERIFY(!controller.commitEdit(QStringLiteral("existing.txt")));
    QVERIFY(controller.activeEdit());
    controller.cancelEdit();
    controller.beginCreate(true, directory.path());
    QVERIFY(!controller.commitEdit(QStringLiteral("existing-folder")));
    QVERIFY(controller.activeEdit());
    controller.cancelEdit();

    QCOMPARE(created.size(), 0);
    QCOMPARE(failed.size(), 2);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("keep this content"));
    QVERIFY(QFileInfo(folderPath).isDir());
}

void ExplorerModelTest::externalParentRemovalCancelsInlineCreate() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString folder = workspaceDirectory.filePath(QStringLiteral("folder"));
    QVERIFY(QDir().mkpath(folder));

    ExplorerFileSystemModel model;
    const QModelIndex root = startLoadingRoot(model, workspaceDirectory.path());
    QTRY_VERIFY_WITH_TIMEOUT(model.index(folder).isValid(), 3000);
    ExplorerTreeView tree(model);
    tree.setRootIndex(root);
    tree.resize(360, 240);
    tree.show();
    tree.beginCreateEditor(model.index(folder), false);
    QTRY_VERIFY_WITH_TIMEOUT(tree.hasInlineEditor(), 3000);

    QSignalSpy cancelled(&tree, &ExplorerTreeView::inlineEditCancelled);
    QVERIFY(QDir(folder).removeRecursively());
    QTRY_COMPARE_WITH_TIMEOUT(cancelled.size(), 1, 5000);
    QVERIFY(!tree.hasInlineEditor());
    QVERIFY(!model.temporaryEntry().isValid());
}

void ExplorerModelTest::deletionUsesConfiguredTrashOperation() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString workspacePath = root.filePath(QStringLiteral("workspace"));
    const QString trashPath = root.filePath(QStringLiteral("trash"));
    QVERIFY(QDir().mkpath(workspacePath));
    QVERIFY(QDir().mkpath(trashPath));
    const QString sourcePath = QDir(workspacePath).filePath(QStringLiteral("example.txt"));
    const QString destinationPath = QDir(trashPath).filePath(QStringLiteral("example.txt"));
    createFile(sourcePath);

    WorkspaceService workspace([destinationPath](const QString& source, QString*) {
        return QFile::rename(source, destinationPath);
    });
    QVERIFY(workspace.openFolder(workspacePath));
    ExplorerController controller(workspace);
    controller.setSelectedPath(sourcePath);

    QSignalSpy deleted(&controller, &ExplorerController::entryDeleted);
    QSignalSpy permanentConfirmation(&controller,
                                     &ExplorerController::permanentDeleteConfirmationRequested);
    controller.requestDelete();

    QCOMPARE(deleted.size(), 1);
    QCOMPARE(permanentConfirmation.size(), 0);
    QVERIFY(!QFileInfo::exists(sourcePath));
    QVERIFY(QFileInfo::exists(destinationPath));
}

void ExplorerModelTest::trashDeleteRejectsReplacementAtConfirmedPath() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString sourcePath = workspaceDirectory.filePath(QStringLiteral("replace-me.txt"));
    createFile(sourcePath);

    WorkspaceService workspace([sourcePath](const QString&, QString* errorMessage) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Trash is unavailable");
        return false;
    });
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    ExplorerController controller(workspace);
    controller.setSelectedPath(sourcePath);
    controller.requestDelete();

    QVERIFY(QFile::remove(sourcePath));
    createFile(sourcePath);
    QSignalSpy permanentConfirmation(&controller,
                                     &ExplorerController::permanentDeleteConfirmationRequested);
    QVERIFY(!controller.confirmDelete(sourcePath));
    QCOMPARE(permanentConfirmation.size(), 1);
    QVERIFY(QFileInfo::exists(sourcePath));
}

void ExplorerModelTest::trashFailureRequiresExplicitPermanentDeletion() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString sourcePath = workspaceDirectory.filePath(QStringLiteral("important.txt"));
    createFile(sourcePath);

    WorkspaceService workspace([](const QString&, QString* errorMessage) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Trash is unavailable");
        return false;
    });
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    ExplorerController controller(workspace);
    controller.setSelectedPath(sourcePath);

    QSignalSpy deleted(&controller, &ExplorerController::entryDeleted);
    QSignalSpy permanentConfirmation(&controller,
                                     &ExplorerController::permanentDeleteConfirmationRequested);
    controller.requestDelete();

    QCOMPARE(permanentConfirmation.size(), 1);
    QCOMPARE(deleted.size(), 0);
    QVERIFY(QFileInfo::exists(sourcePath));

    QVERIFY(controller.confirmPermanentDelete(sourcePath));
    QCOMPARE(deleted.size(), 1);
    QVERIFY(!QFileInfo::exists(sourcePath));
}

void ExplorerModelTest::permanentDeleteRejectsReplacementAtConfirmedPath() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString sourcePath = workspaceDirectory.filePath(QStringLiteral("replace-me.txt"));
    createFile(sourcePath);

    WorkspaceService workspace([sourcePath](const QString&, QString* errorMessage) {
        if (!QFile::remove(sourcePath))
            return false;
        QFile replacement(sourcePath);
        if (!replacement.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return false;
        replacement.write("replacement");
        replacement.close();
        if (errorMessage)
            *errorMessage = QStringLiteral("Trash is unavailable");
        return false;
    });
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    ExplorerController controller(workspace);
    QSignalSpy failed(&controller, &ExplorerController::operationFailed);
    controller.setSelectedPath(sourcePath);
    controller.requestDelete();

    QVERIFY(!controller.confirmPermanentDelete(sourcePath));
    QCOMPARE(failed.size(), 1);
    QFile replacement(sourcePath);
    QVERIFY(replacement.open(QIODevice::ReadOnly));
    QCOMPARE(replacement.readAll(), QByteArrayLiteral("replacement"));
}

void ExplorerModelTest::permanentDeletionRemovesDirectoryLinkOnly() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString target = workspaceDirectory.filePath(QStringLiteral("target"));
    const QString link = workspaceDirectory.filePath(QStringLiteral("link"));
    QVERIFY(QDir().mkpath(target));
    const QString sentinel = QDir(target).filePath(QStringLiteral("keep.txt"));
    createFile(sentinel);

    std::error_code linkError;
#ifdef Q_OS_WIN
    std::filesystem::create_directory_symlink(std::filesystem::path(target.toStdWString()),
                                              std::filesystem::path(link.toStdWString()),
                                              linkError);
#else
    std::filesystem::create_directory_symlink(
        std::filesystem::path(QFile::encodeName(target).constData()),
        std::filesystem::path(QFile::encodeName(link).constData()), linkError);
#endif
    if (linkError && qEnvironmentVariableIsSet("LITECODE_REQUIRE_LINK_TESTS"))
        QFAIL(qPrintable(QStringLiteral("Directory symlink setup failed: %1")
                             .arg(QString::fromStdString(linkError.message()))));
    if (linkError)
        QSKIP("Directory symlinks are not available in this test environment.");

    WorkspaceService workspace([](const QString&, QString* errorMessage) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Trash is unavailable");
        return false;
    });
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    ExplorerController controller(workspace);
    controller.setSelectedPath(link);
    controller.requestDelete();
    QVERIFY(controller.confirmPermanentDelete(link));

    QVERIFY(!QFileInfo::exists(link));
    QVERIFY(QFileInfo::exists(sentinel));
}

void ExplorerModelTest::externalDirectoryLinkIsManagedWithoutFollowingTarget() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString workspacePath = temporary.filePath(QStringLiteral("workspace"));
    const QString target = temporary.filePath(QStringLiteral("outside-target"));
    const QString link = QDir(workspacePath).filePath(QStringLiteral("external-link"));
    QVERIFY(QDir().mkpath(workspacePath));
    QVERIFY(QDir().mkpath(target));
    const QString sentinel = QDir(target).filePath(QStringLiteral("keep.txt"));
    createFile(sentinel);

    std::error_code linkError;
#ifdef Q_OS_WIN
    std::filesystem::create_directory_symlink(std::filesystem::path(target.toStdWString()),
                                              std::filesystem::path(link.toStdWString()),
                                              linkError);
#else
    std::filesystem::create_directory_symlink(
        std::filesystem::path(QFile::encodeName(target).constData()),
        std::filesystem::path(QFile::encodeName(link).constData()), linkError);
#endif
    if (linkError && qEnvironmentVariableIsSet("LITECODE_REQUIRE_LINK_TESTS"))
        QFAIL(qPrintable(QStringLiteral("Directory symlink setup failed: %1")
                             .arg(QString::fromStdString(linkError.message()))));
    if (linkError)
        QSKIP("Directory symlinks are not available in this test environment.");

    WorkspaceService workspace([](const QString&, QString* errorMessage) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Trash is unavailable");
        return false;
    });
    QVERIFY(workspace.openFolder(workspacePath));
    QVERIFY(workspace.containsEntryPath(link));
    QVERIFY(!workspace.containsPath(link));

    QString createError;
    QVERIFY(!workspace.createFile(link, QStringLiteral("must-not-be-created.txt"), nullptr,
                                  &createError));
    QVERIFY(!QFileInfo::exists(QDir(target).filePath(QStringLiteral("must-not-be-created.txt"))));

    ExplorerController controller(workspace);
    controller.setSelectedPath(link);
    QCOMPARE(QDir::cleanPath(controller.selectedPath()), QDir::cleanPath(link));
    controller.requestDelete();
    QVERIFY(controller.confirmPermanentDelete(link));
    QVERIFY(!WorkspaceService::entryExists(link));
    QVERIFY(QFileInfo::exists(sentinel));
}

void ExplorerModelTest::caseOnlyRenameIsAllowedWithoutOverwritingAnotherEntry() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString source = workspaceDirectory.filePath(QStringLiteral("sample.txt"));
    const QString destination = workspaceDirectory.filePath(QStringLiteral("Sample.txt"));
    createFile(source);

    WorkspaceService workspace;
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    QString renamedPath;
    QString error;
    QVERIFY2(workspace.renamePath(source, QStringLiteral("Sample.txt"), &renamedPath, &error),
             qPrintable(error));
    QCOMPARE(QFileInfo(renamedPath).fileName(), QStringLiteral("Sample.txt"));
    QVERIFY(QFileInfo::exists(destination));
}

void ExplorerModelTest::renameRejectsDifferentHardLinkName() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString source = workspaceDirectory.filePath(QStringLiteral("source.txt"));
    const QString destination = workspaceDirectory.filePath(QStringLiteral("alias.txt"));
    createFile(source);

    std::error_code linkError;
#ifdef Q_OS_WIN
    std::filesystem::create_hard_link(std::filesystem::path(source.toStdWString()),
                                      std::filesystem::path(destination.toStdWString()), linkError);
#else
    std::filesystem::create_hard_link(
        std::filesystem::path(QFile::encodeName(source).constData()),
        std::filesystem::path(QFile::encodeName(destination).constData()), linkError);
#endif
    if (linkError && qEnvironmentVariableIsSet("LITECODE_REQUIRE_LINK_TESTS"))
        QFAIL(qPrintable(QStringLiteral("Hard link setup failed: %1")
                             .arg(QString::fromStdString(linkError.message()))));
    if (linkError)
        QSKIP("Hard links are not available in this test environment.");

    WorkspaceService workspace;
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    QString error;
    QVERIFY(!workspace.renamePath(source, QStringLiteral("alias.txt"), nullptr, &error));
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(destination));
}

void ExplorerModelTest::explorerRenameRebindsOpenDocument() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString directory = workspaceDirectory.filePath(QStringLiteral("before"));
    const QString renamedDirectory = workspaceDirectory.filePath(QStringLiteral("after"));
    QVERIFY(QDir().mkpath(directory));
    const QString original = QDir(directory).filePath(QStringLiteral("document.txt"));
    const QString renamed = QDir(renamedDirectory).filePath(QStringLiteral("document.txt"));
    createFile(original);

    EditorArea editor;
    QSignalSpy opened(&editor, &EditorArea::documentOpened);
    QVERIFY(editor.openFile(original));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    WorkspaceService workspace;
    QVERIFY(workspace.openFolder(workspaceDirectory.path()));
    ExplorerController controller(workspace);
    connect(&controller, &ExplorerController::entryRenamed, &editor, &EditorArea::rebindPaths);
    controller.setSelectedPath(directory);
    controller.beginRename();
    QVERIFY(controller.commitEdit(QStringLiteral("after")));

    QVERIFY(litecode::core::pathsReferToSameEntry(editor.currentFile(), renamed));
    QVERIFY(editor.saveCurrent());
    QVERIFY(!QFileInfo::exists(original));
    QVERIFY(QFileInfo::exists(renamed));
}

void ExplorerModelTest::deletingPathKeepsAffectedDocumentsAsDeleted() {
    QTemporaryDir workspaceDirectory;
    QVERIFY(workspaceDirectory.isValid());
    const QString folder = workspaceDirectory.filePath(QStringLiteral("folder"));
    QVERIFY(QDir().mkpath(folder));
    const QString affected = QDir(folder).filePath(QStringLiteral("affected.txt"));
    const QString unaffected = workspaceDirectory.filePath(QStringLiteral("unaffected.txt"));
    createFile(affected);
    createFile(unaffected);

    EditorArea editor;
    EditorSessionController sessions(editor);
    QSignalSpy opened(&editor, &EditorArea::documentOpened);
    QVERIFY(sessions.openFile(affected));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    QVERIFY(sessions.openFile(unaffected));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 5000);

    QCOMPARE(sessions.documentsAffectedByPath(folder).size(), 1);
    QVERIFY(QDir(folder).removeRecursively());
    sessions.handlePathDeleted(folder);
    QCOMPARE(sessions.openFiles().size(), 2);
    const auto openFiles = sessions.openFiles();
    const auto containsPath = [&openFiles](const QString& expected) {
        return std::any_of(openFiles.cbegin(), openFiles.cend(), [&expected](const QString& open) {
            return litecode::core::pathsReferToSameEntry(open, expected);
        });
    };
    QVERIFY(containsPath(affected));
    QVERIFY(containsPath(unaffected));

    auto* tabs = editor.findChild<QTabWidget*>(QStringLiteral("editorTabs"));
    QVERIFY(tabs != nullptr);
    const int affectedTab = tabs->indexOf(tabs->widget(0));
    QCOMPARE(tabs->tabText(affectedTab), QStringLiteral("affected.txt"));
    QVERIFY(tabs->tabBar()->tabData(affectedTab).toBool());

    // Navigation can still reactivate an already-open orphaned editor even though its file is
    // absent from disk.
    QVERIFY(sessions.openFile(affected));
    QVERIFY(litecode::core::pathsReferToSameEntry(sessions.currentFile(), affected));

    QVERIFY(QDir().mkpath(folder));
    createFile(affected);
    QTRY_VERIFY_WITH_TIMEOUT(!tabs->tabBar()->tabData(affectedTab).toBool(), 5000);
}

void ExplorerModelTest::externalDeletionMarksOpenEditorAsDeleted() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("removed.txt"));
    createFile(path);

    EditorArea editor;
    QSignalSpy opened(&editor, &EditorArea::documentOpened);
    QVERIFY(editor.openFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);

    QVERIFY(QFile::remove(path));
    auto* tabs = editor.findChild<QTabWidget*>(QStringLiteral("editorTabs"));
    QVERIFY(tabs != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(tabs->tabBar()->tabData(0).toBool(), 5000);
    QCOMPARE(tabs->tabText(0), QStringLiteral("removed.txt"));
    QVERIFY(!tabs->tabIcon(0).isNull());
    QVERIFY(tabs->tabBar()->tabButton(0, QTabBar::LeftSide) == nullptr);
    QVERIFY(editor.saveCurrent()); // Clean saves are no-ops, including orphaned editors.
    QVERIFY(!QFileInfo::exists(path));

    createFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(!tabs->tabBar()->tabData(0).toBool(), 5000);
}

void ExplorerModelTest::deletedStateRemainsIndependentFromUnsavedEdits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("removed.txt"));
    createFile(path);

    litecode::editor::DocumentSession document(litecode::core::DocumentId(1), path);
    const litecode::editor::DocumentLoadResult load =
        litecode::editor::DocumentSession::loadBounded(path);
    QVERIFY(document.acceptLoad(load));
    document.markDeleted();
    document.setModified(true);
    QVERIFY(document.isDeleted());
    QVERIFY(document.isModified());
    QCOMPARE(document.state(), litecode::editor::DocumentState::Deleted);
    document.markRestored();
    QVERIFY(!document.isDeleted());
    QVERIFY(document.isModified());
}

void ExplorerModelTest::saveAsRejectsPathOpenInAnotherTab() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.txt"));
    const QString second = directory.filePath(QStringLiteral("second.txt"));
    createFile(first);
    createFile(second);

    EditorArea editor;
    QSignalSpy opened(&editor, &EditorArea::documentOpened);
    QVERIFY(editor.openFile(first));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 5000);
    QVERIFY(editor.openFile(second));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 5000);

    QString error;
    QVERIFY(!editor.saveCurrentAs(first, &error));
    QVERIFY(error.contains(QStringLiteral("already open"), Qt::CaseInsensitive));
}

QTEST_MAIN(ExplorerModelTest)
#include "tst_explorer_model.moc"
