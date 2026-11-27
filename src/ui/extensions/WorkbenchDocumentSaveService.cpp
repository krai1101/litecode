#include "ui/extensions/WorkbenchDocumentSaveService.h"
#include "core/FileSystemPath.h"
#include "ui/EditorArea.h"
#include <QFileDialog>

namespace litecode::ui {
WorkbenchDocumentSaveService::WorkbenchDocumentSaveService(EditorArea* editors,
                                                           QWidget* dialogParent, QObject* parent)
    : QObject(parent), editors_(editors), dialogParent_(dialogParent) {}
std::optional<QUrl> WorkbenchDocumentSaveService::saveResource(const QUrl& uri, bool saveAs,
                                                               QString* error) {
    if (error)
        error->clear();
    const auto editors = editors_;
    if (!editors) {
        if (error)
            *error = tr("The editor service has stopped.");
        return {};
    }
    std::optional<EditorDocumentInfo> selected;
    for (const auto& document : editors->documents()) {
        const auto model = editors->documentModels().find(document.documentId);
        if ((model && model->resource() == uri) ||
            (uri.isLocalFile() && !document.filePath.isEmpty() &&
             core::pathsReferToSameEntry(document.filePath, uri.toLocalFile()))) {
            selected = document;
            break;
        }
    }
    if (!selected)
        return {}; // Missing editors resolve undefined, not rejection.
    const auto id = selected->documentId;
    if (!saveAs) {
        const auto model = editors->documentModels().find(id);
        if (model && model->session().filePath().isEmpty() &&
            model->resource().scheme() != "untitled")
            return {};
    }
    bool saved = false;
    QString failure;
    if (saveAs || selected->filePath.isEmpty()) {
        const auto target =
            QFileDialog::getSaveFileName(dialogParent_, tr("Save File As"), selected->filePath);
        if (target.isEmpty())
            return {};
        if (!editors) {
            if (error)
                *error = QStringLiteral("The editor service has stopped.");
            return {};
        }
        saved = editors->saveDocumentAs(id, target, &failure);
    } else
        saved = editors->saveDocument(id, &failure, true);
    if (!saved || !editors)
        return {};
    if (const auto model = editors->documentModels().find(id))
        return model->resource();
    return {};
}
bool WorkbenchDocumentSaveService::saveAll(bool includeUntitled, QString* error) {
    if (error)
        error->clear();
    const auto editors = editors_;
    if (!editors) {
        if (error)
            *error = tr("The editor service has stopped.");
        return false;
    }
    // Capture identities rather than retaining Page pointers across save dialogs.
    const auto documents = editors->documents();
    const QPointer<WorkbenchDocumentSaveService> alive(this);
    bool success = true;
    for (const auto& document : documents) {
        if (!document.modified || (!includeUntitled && document.filePath.isEmpty()))
            continue;
        if (!alive || !editors)
            return false;
        const auto model = editors->documentModels().find(document.documentId);
        if (!model || !alive->saveResource(model->resource(), false, error))
            success = false;
        if (error && !error->isEmpty())
            return false;
    }
    return success;
}
} // namespace litecode::ui
