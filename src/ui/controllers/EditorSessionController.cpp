#include "ui/controllers/EditorSessionController.h"
#include "core/FileSystemPath.h"

#include "ui/EditorArea.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace litecode::ui {
namespace {

QString normalizedPath(const QString& path) { return core::normalizedFileSystemPath(path); }

bool isEqualOrDescendant(const QString& candidatePath, const QString& parentPath) {
    const QString candidate = normalizedPath(candidatePath);
    const QString parent = normalizedPath(parentPath);
    const Qt::CaseSensitivity sensitivity = core::fileSystemCaseSensitivity(parent);
    return core::pathsReferToSameEntry(candidate, parent) ||
           candidate.startsWith(parent + QLatin1Char('/'), sensitivity);
}

} // namespace

EditorSessionController::EditorSessionController(EditorArea& editorArea, QObject* parent)
    : QObject(parent), editorArea_(&editorArea) {
    connect(&editorArea, &EditorArea::currentFileChanged, this, [this](const QString& filePath) {
        recordNavigation(filePath);
        emit currentFileChanged(filePath);
    });
    connect(&editorArea, &EditorArea::openFilesChanged, this,
            &EditorSessionController::openFilesChanged);
    connect(&editorArea, &EditorArea::currentEncodingChanged, this,
            &EditorSessionController::currentEncodingChanged);
    connect(&editorArea, &EditorArea::documentCloseRequested, this,
            &EditorSessionController::handleDocumentCloseRequest);
    connect(&editorArea, &EditorArea::externalModificationDetected, this,
            &EditorSessionController::handleExternalModification);
    connect(&editorArea, &EditorArea::fileOpenFailed, this,
            [this](const QString& filePath, const QString& diagnostic) {
                const QString message =
                    tr("Unable to open %1: %2").arg(QFileInfo(filePath).fileName(), diagnostic);
                emit statusMessageRequested(message, 8000);
            });
    connect(&editorArea, &EditorArea::fileReloadFailed, this,
            [this](const QString& filePath, const QString& diagnostic) {
                emit statusMessageRequested(
                    tr("Unable to reload %1: %2").arg(QFileInfo(filePath).fileName(), diagnostic),
                    8000);
            });
    connect(&editorArea, &EditorArea::largeFileModeActivated, this,
            [this](const QString& filePath) {
                emit statusMessageRequested(
                    tr("%1: syntax highlighting was disabled for this large file.")
                        .arg(QFileInfo(filePath).fileName()),
                    8000);
            });
}

void EditorSessionController::setConfirmationHandler(ConfirmationHandler handler) {
    confirmationHandler_ = std::move(handler);
}

void EditorSessionController::setWorkspacePath(const QString& workspacePath) {
    const QString resolved =
        workspacePath.isEmpty() ? QString{} : QFileInfo(workspacePath).absoluteFilePath();
    if (resolved == workspacePath_)
        return;

    workspacePath_ = resolved;
    if (editorArea_)
        editorArea_->setWorkspaceRoot(workspacePath_);
}

QString EditorSessionController::workspacePath() const { return workspacePath_; }

QString EditorSessionController::currentFile() const {
    return editorArea_ ? editorArea_->currentFile() : QString{};
}

core::TextEncoding EditorSessionController::currentEncoding() const {
    return editorArea_ ? editorArea_->currentEncoding() : core::TextEncoding::Utf8;
}

QStringList EditorSessionController::openFiles() const {
    return editorArea_ ? editorArea_->openFiles() : QStringList{};
}

bool EditorSessionController::canNavigateBack() const { return navigationIndex_ > 0; }

bool EditorSessionController::canNavigateForward() const {
    return navigationIndex_ >= 0 && navigationIndex_ + 1 < navigationHistory_.size();
}

bool EditorSessionController::openFile(const QString& filePath, QString* errorMessage) {
    return editorArea_ && editorArea_->openFile(filePath, errorMessage);
}

bool EditorSessionController::openFileWithEncoding(const QString& filePath,
                                                   core::TextEncoding encoding,
                                                   QString* errorMessage) {
    return editorArea_ && editorArea_->openFile(filePath, errorMessage, encoding);
}

bool EditorSessionController::openLocation(const QString& filePath, int line,
                                           QString* errorMessage) {
    if (!openFile(filePath, errorMessage))
        return false;
    if (editorArea_)
        editorArea_->goToLine(line);
    return true;
}

bool EditorSessionController::openMatch(const QString& filePath, int line, int column, int length,
                                        QString* errorMessage) {
    if (!openFile(filePath, errorMessage))
        return false;
    if (editorArea_)
        editorArea_->goToMatch(line, column, length);
    return true;
}

bool EditorSessionController::saveCurrent(QString* errorMessage) {
    return editorArea_ && editorArea_->saveCurrent(errorMessage);
}

bool EditorSessionController::saveCurrentWithEncoding(core::TextEncoding encoding,
                                                      QString* errorMessage) {
    return editorArea_ && editorArea_->saveCurrentWithEncoding(encoding, errorMessage);
}

bool EditorSessionController::saveCurrentAs(const QString& filePath, QString* errorMessage) {
    return editorArea_ && editorArea_->saveCurrentAs(filePath, errorMessage);
}

bool EditorSessionController::reopenCurrentWithEncoding(core::TextEncoding encoding,
                                                        QString* errorMessage) {
    return editorArea_ && editorArea_->reopenCurrentWithEncoding(encoding, errorMessage);
}

bool EditorSessionController::closeAllForWorkspaceChange() {
    if (!editorArea_)
        return true;

    const QVector<EditorDocumentInfo> openDocuments = editorArea_->documents();
    for (const EditorDocumentInfo& info : openDocuments) {
        if (!info.modified)
            continue;
        const EditorDecision decision =
            decide({EditorConfirmationKind::CloseDocument, info.documentId, info.filePath,
                    info.displayName, true});
        if (decision == EditorDecision::Cancel)
            return false;
        if (decision == EditorDecision::Save) {
            QString error;
            if (!editorArea_->saveDocument(info.documentId, &error)) {
                emit operationFailed(tr("Save failed"), error);
                return false;
            }
        }
    }

    for (const EditorDocumentInfo& info : openDocuments)
        editorArea_->closeDocument(info.documentId);
    navigationHistory_.clear();
    navigationIndex_ = -1;
    updateNavigationAvailability();
    return true;
}

bool EditorSessionController::closeAllForApplicationExit() {
    if (!editorArea_)
        return true;
    const QVector<EditorDocumentInfo> openDocuments = editorArea_->documents();
    for (const EditorDocumentInfo& info : openDocuments) {
        if (!info.modified)
            continue;
        const EditorDecision decision =
            decide({EditorConfirmationKind::CloseApplication, info.documentId, info.filePath,
                    info.displayName, true});
        if (decision == EditorDecision::Cancel)
            return false;
        if (decision == EditorDecision::Save) {
            QString error;
            if (!editorArea_->saveDocument(info.documentId, &error)) {
                emit operationFailed(tr("Save failed"), error);
                return false;
            }
        }
    }
    return true;
}

QVector<EditorDocumentInfo>
EditorSessionController::documentsAffectedByPath(const QString& path) const {
    QVector<EditorDocumentInfo> affected;
    if (!editorArea_ || path.isEmpty())
        return affected;
    for (const EditorDocumentInfo& document : editorArea_->documents()) {
        if (isEqualOrDescendant(document.filePath, path))
            affected.push_back(document);
    }
    return affected;
}

void EditorSessionController::handlePathRenamed(const QString& previousPath, const QString& path) {
    if (!editorArea_)
        return;
    editorArea_->rebindPaths(previousPath, path);

    const QString previous = QDir::fromNativeSeparators(QDir::cleanPath(previousPath));
    const QString replacement = QDir::fromNativeSeparators(QDir::cleanPath(path));
    const Qt::CaseSensitivity sensitivity = core::fileSystemCaseSensitivity(previous);
    for (QString& entry : navigationHistory_) {
        const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(entry));
        if (normalized.compare(previous, sensitivity) == 0)
            entry = replacement;
        else if (normalized.startsWith(previous + QLatin1Char('/'), sensitivity))
            entry = replacement + normalized.sliced(previous.size());
    }
}

void EditorSessionController::handlePathDeleted(const QString& path) {
    if (!editorArea_ || path.isEmpty())
        return;

    const QVector<EditorDocumentInfo> affected = documentsAffectedByPath(path);
    for (const EditorDocumentInfo& document : affected)
        editorArea_->markDocumentDeleted(document.documentId);
}

void EditorSessionController::navigateBack() { navigateBy(-1); }

void EditorSessionController::navigateForward() { navigateBy(1); }

void EditorSessionController::recordNavigation(const QString& filePath) {
    if (filePath.isEmpty() || navigationInProgress_)
        return;
    if (navigationIndex_ >= 0 && navigationHistory_.at(navigationIndex_) == filePath)
        return;
    navigationHistory_ = navigationHistory_.mid(0, navigationIndex_ + 1);
    navigationHistory_.append(filePath);
    navigationIndex_ = navigationHistory_.size() - 1;
    updateNavigationAvailability();
}

void EditorSessionController::navigateBy(int offset) {
    if (!editorArea_)
        return;
    const int target = navigationIndex_ + offset;
    if (target < 0 || target >= navigationHistory_.size())
        return;
    const int previous = navigationIndex_;
    navigationInProgress_ = true;
    QString error;
    if (editorArea_->openFile(navigationHistory_.at(target), &error)) {
        navigationIndex_ = target;
    } else {
        navigationIndex_ = previous;
        emit statusMessageRequested(error, 5000);
    }
    navigationInProgress_ = false;
    updateNavigationAvailability();
}

void EditorSessionController::updateNavigationAvailability() {
    emit navigationAvailabilityChanged(canNavigateBack(), canNavigateForward());
}

void EditorSessionController::handleDocumentCloseRequest(core::DocumentId documentId) {
    if (!editorArea_)
        return;
    const auto info = document(documentId);
    if (!info)
        return;
    if (info->modified) {
        const EditorDecision decision =
            decide({EditorConfirmationKind::CloseDocument, info->documentId, info->filePath,
                    info->displayName, true});
        if (decision == EditorDecision::Cancel)
            return;
        if (decision == EditorDecision::Save) {
            QString error;
            if (!editorArea_->saveDocument(documentId, &error)) {
                emit operationFailed(tr("Save failed"), error);
                return;
            }
        }
    }
    editorArea_->closeDocument(documentId);
}

void EditorSessionController::handleExternalModification(const ExternalDocumentChange& change) {
    if (!editorArea_)
        return;
    const EditorDecision decision =
        decide({EditorConfirmationKind::ExternalModification, change.documentId, change.filePath,
                change.displayName, change.hasUnsavedChanges});
    if (decision == EditorDecision::Reload) {
        QString error;
        if (!editorArea_->reloadDocument(change.documentId, &error))
            emit operationFailed(tr("Reload failed"), error);
        return;
    }
    editorArea_->resumeWatching(change.documentId);
}

EditorDecision EditorSessionController::decide(const EditorConfirmation& request) const {
    if (confirmationHandler_)
        return confirmationHandler_(request);
    return request.kind == EditorConfirmationKind::ExternalModification ? EditorDecision::Keep
                                                                        : EditorDecision::Cancel;
}

std::optional<EditorDocumentInfo>
EditorSessionController::document(core::DocumentId documentId) const {
    if (!editorArea_)
        return std::nullopt;
    for (const EditorDocumentInfo& info : editorArea_->documents()) {
        if (info.documentId == documentId)
            return info;
    }
    return std::nullopt;
}

} // namespace litecode::ui
