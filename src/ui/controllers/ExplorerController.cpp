#include "ui/controllers/ExplorerController.h"

#include <QDir>
#include <QFileInfo>

namespace litecode::ui {

ExplorerController::ExplorerController(workspace::WorkspaceService& workspace, QObject* parent)
    : QObject(parent), workspace_(workspace) {
    connect(&workspace_, &workspace::WorkspaceService::workspaceChanged, this,
            [this](const QString& rootPath) {
                selectedPath_.clear();
                pendingDeletePath_.clear();
                pendingDeleteIdentity_.clear();
                activeEdit_.reset();
                emit workspaceRootChanged(rootPath);
            });
}

ExplorerController::~ExplorerController() = default;

QString ExplorerController::workspaceRoot() const { return workspace_.rootPath(); }

bool ExplorerController::hasWorkspace() const { return workspace_.hasWorkspace(); }

QString ExplorerController::selectedPath() const { return selectedPath_; }

const ExplorerEditRequest* ExplorerController::activeEdit() const { return activeEdit_.get(); }

bool ExplorerController::openWorkspace(const QString& folderPath, QString* errorMessage) {
    return workspace_.openFolder(folderPath, errorMessage);
}

void ExplorerController::setSelectedPath(const QString& path) {
    selectedPath_ = resolvedSelection(path);
}

void ExplorerController::requestRefresh() {
    if (workspace_.hasWorkspace())
        emit refreshRequested(workspace_.rootPath());
}

void ExplorerController::beginCreate(bool directory, const QString& parentDirectory) {
    if (!workspace_.hasWorkspace()) {
        emit operationFailed(tr("Open a workspace folder before creating an item."));
        return;
    }
    QString requestedParent = parentDirectory;
    if (requestedParent.isEmpty() && activeEdit_ &&
        (activeEdit_->kind == ExplorerEditKind::NewFile ||
         activeEdit_->kind == ExplorerEditKind::NewDirectory)) {
        requestedParent = activeEdit_->parentDirectory;
    }
    const QString parent = resolvedCreationParent(requestedParent);
    if (parent.isEmpty()) {
        emit operationFailed(tr("Choose a folder inside the workspace."));
        return;
    }
    // An inline editor may lose focus only after the toolbar/context-menu action has
    // started. End the old edit before publishing the next request so that stale
    // focus events cannot cancel the new operation.
    if (activeEdit_) {
        activeEdit_.reset();
        emit editCancelled();
    }
    activeEdit_ = std::make_unique<ExplorerEditRequest>(ExplorerEditRequest{
        directory ? ExplorerEditKind::NewDirectory : ExplorerEditKind::NewFile, parent, {}, {}});
    emit editRequested(*activeEdit_);
}

void ExplorerController::beginRename(const QString& path) {
    const QString source = resolvedSelection(path.isEmpty() ? selectedPath_ : path);
    if (source.isEmpty() || source == workspace_.rootPath()) {
        emit operationFailed(tr("Choose an item inside the workspace to rename."));
        return;
    }
    if (activeEdit_) {
        activeEdit_.reset();
        emit editCancelled();
    }
    const QFileInfo info(source);
    activeEdit_ = std::make_unique<ExplorerEditRequest>(ExplorerEditRequest{
        ExplorerEditKind::Rename, info.dir().absolutePath(), source, info.fileName()});
    emit editRequested(*activeEdit_);
}

bool ExplorerController::commitEdit(const QString& name) {
    if (!activeEdit_)
        return false;

    const ExplorerEditRequest request = *activeEdit_;
    QString path;
    QString error;
    bool succeeded = false;
    switch (request.kind) {
    case ExplorerEditKind::NewFile:
        succeeded = workspace_.createFile(request.parentDirectory, name, &path, &error);
        break;
    case ExplorerEditKind::NewDirectory:
        succeeded = workspace_.createDirectory(request.parentDirectory, name, &path, &error);
        break;
    case ExplorerEditKind::Rename:
        succeeded = workspace_.renamePath(request.sourcePath, name, &path, &error);
        break;
    }
    if (!succeeded) {
        emit operationFailed(error);
        return false;
    }

    activeEdit_.reset();
    selectedPath_ = path;
    if (request.kind == ExplorerEditKind::Rename)
        emit entryRenamed(request.sourcePath, path);
    else
        emit entryCreated(path, request.kind == ExplorerEditKind::NewDirectory);
    return true;
}

void ExplorerController::cancelEdit() {
    if (!activeEdit_)
        return;
    activeEdit_.reset();
    emit editCancelled();
}

void ExplorerController::requestDelete() {
    const QString path = resolvedSelection(selectedPath_);
    if (path.isEmpty() || path == workspace_.rootPath()) {
        emit operationFailed(tr("Choose an item inside the workspace to delete."));
        return;
    }
    pendingDeletePath_ = path;
    pendingDeleteIdentity_ = workspace::WorkspaceService::entryIdentity(path);
    if (pendingDeleteIdentity_.isEmpty()) {
        pendingDeletePath_.clear();
        emit operationFailed(tr("The selected item could not be identified safely."));
        return;
    }
    (void)confirmDelete(path);
}

bool ExplorerController::confirmDelete(const QString& path) {
    const QString candidate = resolvedSelection(path);
    if (candidate.isEmpty() || candidate != pendingDeletePath_) {
        emit operationFailed(tr("The item selected for deletion has changed."));
        return false;
    }

    QString error;
    if (!workspace_.movePathToTrash(candidate, pendingDeleteIdentity_, &error)) {
        const QFileInfo info(candidate);
        emit permanentDeleteConfirmationRequested({candidate, info.fileName(), info.isDir()},
                                                  error);
        return false;
    }

    pendingDeletePath_.clear();
    pendingDeleteIdentity_.clear();
    selectedPath_.clear();
    emit entryDeleted(candidate, false);
    return true;
}

bool ExplorerController::confirmPermanentDelete(const QString& path) {
    const QString candidate = resolvedSelection(path);
    if (candidate.isEmpty() || candidate != pendingDeletePath_) {
        emit operationFailed(tr("The item selected for deletion has changed."));
        return false;
    }

    QString error;
    if (!workspace_.removePath(candidate, pendingDeleteIdentity_, &error)) {
        pendingDeletePath_.clear();
        pendingDeleteIdentity_.clear();
        emit operationFailed(error);
        return false;
    }

    pendingDeletePath_.clear();
    pendingDeleteIdentity_.clear();
    selectedPath_.clear();
    emit entryDeleted(candidate, true);
    return true;
}

void ExplorerController::cancelDelete(const QString& path) {
    if (path == pendingDeletePath_) {
        pendingDeletePath_.clear();
        pendingDeleteIdentity_.clear();
    }
}

QString ExplorerController::resolvedCreationParent(const QString& requestedParent) const {
    QString parent = requestedParent;
    if (parent.isEmpty() && !selectedPath_.isEmpty()) {
        const QFileInfo selection(selectedPath_);
        parent = selection.isDir() ? selection.absoluteFilePath() : selection.dir().absolutePath();
    }
    if (parent.isEmpty())
        parent = workspace_.rootPath();
    if (!workspace_.containsPath(parent) || !QFileInfo(parent).isDir())
        return {};
    return QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(parent).absoluteFilePath()));
}

QString ExplorerController::resolvedSelection(const QString& requestedPath) const {
    if (requestedPath.isEmpty() || !workspace_.containsEntryPath(requestedPath) ||
        !workspace::WorkspaceService::entryExists(requestedPath)) {
        return {};
    }
    return QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(requestedPath).absoluteFilePath()));
}

} // namespace litecode::ui
