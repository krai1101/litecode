#pragma once

#include "workspace/WorkspaceService.h"

#include <QObject>
#include <QString>

#include <memory>

namespace litecode::ui {

enum class ExplorerEditKind { NewFile, NewDirectory, Rename };

struct ExplorerEditRequest final {
    ExplorerEditKind kind{ExplorerEditKind::NewFile};
    QString parentDirectory;
    QString sourcePath;
    QString initialName;
};

struct ExplorerDeleteRequest final {
    QString path;
    QString displayName;
    bool directory{false};
};

class ExplorerController final : public QObject {
    Q_OBJECT

  public:
    explicit ExplorerController(workspace::WorkspaceService& workspace, QObject* parent = nullptr);
    ~ExplorerController() override;

    [[nodiscard]] QString workspaceRoot() const;
    [[nodiscard]] bool hasWorkspace() const;
    [[nodiscard]] QString selectedPath() const;
    [[nodiscard]] const ExplorerEditRequest* activeEdit() const;

    bool openWorkspace(const QString& folderPath, QString* errorMessage = nullptr);
    void setSelectedPath(const QString& path);
    void requestRefresh();
    void beginCreate(bool directory, const QString& parentDirectory = {});
    void beginRename(const QString& path = {});
    bool commitEdit(const QString& name);
    void cancelEdit();
    void requestDelete();
    bool confirmDelete(const QString& path);
    bool confirmPermanentDelete(const QString& path);
    void cancelDelete(const QString& path);

  signals:
    void workspaceRootChanged(const QString& rootPath);
    void refreshRequested(const QString& rootPath);
    void editRequested(const litecode::ui::ExplorerEditRequest& request);
    void editCancelled();
    void entryCreated(const QString& path, bool directory);
    void entryRenamed(const QString& previousPath, const QString& path);
    void permanentDeleteConfirmationRequested(const litecode::ui::ExplorerDeleteRequest& request,
                                              const QString& reason);
    void entryDeleted(const QString& path, bool permanently);
    void operationFailed(const QString& message);

  private:
    [[nodiscard]] QString resolvedCreationParent(const QString& requestedParent) const;
    [[nodiscard]] QString resolvedSelection(const QString& requestedPath) const;

    workspace::WorkspaceService& workspace_;
    QString selectedPath_;
    std::unique_ptr<ExplorerEditRequest> activeEdit_;
    QString pendingDeletePath_;
    QByteArray pendingDeleteIdentity_;
};

} // namespace litecode::ui

Q_DECLARE_METATYPE(litecode::ui::ExplorerEditRequest)
Q_DECLARE_METATYPE(litecode::ui::ExplorerDeleteRequest)
