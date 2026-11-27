#include "ui/extensions/FileDialogDefaults.h"
#include "ui/controllers/EditorSessionController.h"
#include "ui/controllers/WorkbenchController.h"
#include "extensions/ConfigurationService.h"
#include <QDir>
#include <QFileInfo>

namespace litecode::ui {
FileDialogDefaults::FileDialogDefaults(EditorSessionController& editors, WorkbenchController& workbench,
    extensions::ConfigurationService& configuration, QObject* parent)
    : QObject(parent), lastFile_(editors.currentFile()), lastRoot_(workbench.workspacePath()), configuration_(&configuration) {
    connect(&editors, &EditorSessionController::currentFileChanged, this, [this](const QString& path) {
        if (!path.isEmpty()) lastFile_ = path;
    });
    connect(&workbench, &WorkbenchController::workspaceChanged, this, [this](const QString& path) {
        if (!path.isEmpty()) lastRoot_ = path;
    });
}
QUrl FileDialogDefaults::defaultUri() const {
    // Mirrors upstream defaultFilePath: last active file's parent, then the
    // last active workspace root, then the configured preferred home/home.
    if (!lastFile_.isEmpty()) return QUrl::fromLocalFile(QFileInfo(lastFile_).absolutePath());
    if (!lastRoot_.isEmpty()) return QUrl::fromLocalFile(lastRoot_);
    if (configuration_) {
        const auto value = configuration_->model().inspect("files.dialog.defaultPath");
        if (value && value->globalValue.isString()) {
            const auto path = value->globalValue.toString();
            if (QDir::isAbsolutePath(path) && QFileInfo::exists(path)) return QUrl::fromLocalFile(QDir::cleanPath(path));
        }
    }
    return QUrl::fromLocalFile(QDir::homePath());
}
} // namespace litecode::ui
