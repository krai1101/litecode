#pragma once

#include "core/DocumentTypes.h"
#include "ui/EditorArea.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

#include <functional>
#include <optional>

namespace litecode::ui {

enum class EditorConfirmationKind {
    CloseDocument,
    CloseApplication,
    ExternalModification,
};

enum class EditorDecision {
    Save,
    Discard,
    Cancel,
    Reload,
    Keep,
};

struct EditorConfirmation final {
    EditorConfirmationKind kind{EditorConfirmationKind::CloseDocument};
    core::DocumentId documentId;
    QString filePath;
    QString displayName;
    bool hasUnsavedChanges{};
};

class EditorSessionController final : public QObject {
    Q_OBJECT

  public:
    using ConfirmationHandler = std::function<EditorDecision(const EditorConfirmation&)>;

    explicit EditorSessionController(EditorArea& editorArea, QObject* parent = nullptr);

    void setConfirmationHandler(ConfirmationHandler handler);
    void setWorkspacePath(const QString& workspacePath);
    [[nodiscard]] QString workspacePath() const;
    [[nodiscard]] QString currentFile() const;
    [[nodiscard]] core::TextEncoding currentEncoding() const;
    [[nodiscard]] QStringList openFiles() const;
    [[nodiscard]] bool canNavigateBack() const;
    [[nodiscard]] bool canNavigateForward() const;

    bool openFile(const QString& filePath, QString* errorMessage = nullptr);
    bool openFileWithEncoding(const QString& filePath, core::TextEncoding encoding,
                              QString* errorMessage = nullptr);
    bool openLocation(const QString& filePath, int line, QString* errorMessage = nullptr);
    bool openMatch(const QString& filePath, int line, int column, int length,
                   QString* errorMessage = nullptr);
    bool saveCurrent(QString* errorMessage = nullptr);
    bool saveCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage = nullptr);
    bool saveCurrentAs(const QString& filePath, QString* errorMessage = nullptr);
    bool reopenCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage = nullptr);
    bool closeAllForWorkspaceChange();
    bool closeAllForApplicationExit();
    [[nodiscard]] QVector<EditorDocumentInfo> documentsAffectedByPath(const QString& path) const;
    void handlePathRenamed(const QString& previousPath, const QString& path);
    void handlePathDeleted(const QString& path);
    void navigateBack();
    void navigateForward();

  signals:
    void currentFileChanged(const QString& filePath);
    void currentEncodingChanged(litecode::core::TextEncoding encoding);
    void openFilesChanged();
    void navigationAvailabilityChanged(bool canGoBack, bool canGoForward);
    void statusMessageRequested(const QString& message, int timeoutMs);
    void operationFailed(const QString& title, const QString& message);

  private:
    void recordNavigation(const QString& filePath);
    void navigateBy(int offset);
    void updateNavigationAvailability();
    void handleDocumentCloseRequest(core::DocumentId documentId);
    void handleExternalModification(const ExternalDocumentChange& change);
    [[nodiscard]] EditorDecision decide(const EditorConfirmation& request) const;
    [[nodiscard]] std::optional<EditorDocumentInfo> document(core::DocumentId documentId) const;

    QPointer<EditorArea> editorArea_;
    ConfirmationHandler confirmationHandler_;
    QString workspacePath_;
    QStringList navigationHistory_;
    int navigationIndex_{-1};
    bool navigationInProgress_{false};
};

} // namespace litecode::ui

Q_DECLARE_METATYPE(litecode::ui::EditorConfirmation)
