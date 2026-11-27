#pragma once

#include "core/DocumentTypes.h"
#include "editor/DocumentModelStore.h"
#include "extensions/DocumentWire.h"
#include "extensions/EditorStateWire.h"

#include <QFont>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <QHash>
#include <QMap>
#include <QSet>

#include <memory>
#include <optional>
#include <vector>

class QTabWidget;
class QFileSystemWatcher;
class QFrame;
class QLineEdit;
class QLabel;
class QStackedWidget;
class QToolButton;
class QResizeEvent;
class QEvent;
class QScrollBar;

namespace litecode::editor {
struct DocumentLoadResult;
}
namespace litecode::workspace {
struct GitComparison;
}
namespace litecode::core {
class SettingsService;
}
namespace litecode::extensions { struct TextEditorEditData; }

namespace litecode::ui {
class ExplorerFileSystemModel;
struct DocumentEditPlan final {
    core::DocumentId id;
    qint64 expectedVersion{};
    QVector<core::DocumentEdit> edits;
    std::optional<core::LineEnding> newEol;
};

namespace components {
class WorkbenchTabs;
class EditorFindReplaceController;
class FindTextInput;
} // namespace components

struct EditorDocumentInfo final {
    core::DocumentId documentId;
    QString filePath;
    QString displayName;
    qint64 version{};
    bool modified{};
    bool loaded{};
    core::TextEncoding encoding{core::TextEncoding::Utf8};
};

struct ExternalDocumentChange final {
    core::DocumentId documentId;
    QString filePath;
    QString displayName;
    bool hasUnsavedChanges{};
};

struct FormatDocumentSnapshot final {
    core::DocumentId documentId;
    QString languageId;
    QString text;
    quint64 revision{};
};

class EditorArea final : public QWidget {
    Q_OBJECT

  public:
    explicit EditorArea(QWidget* parent = nullptr);
    ~EditorArea() override;

    bool openFile(const QString& filePath, QString* errorMessage = nullptr,
                  std::optional<core::TextEncoding> requestedEncoding = std::nullopt);
    void openAuxiliaryTab(const QString& key, const QString& title, QWidget* content,
                          bool replaceExisting = false, const QString& iconResource = {});
    void openGitComparison(const workspace::GitComparison& comparison);
    void openResourceComparison(const QUrl& left, const QUrl& right, const QString& before,
                                const QString& after, const QString& title, bool preserveFocus = false);

    bool saveCurrent(QString* errorMessage = nullptr);
    bool saveCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage = nullptr);
    bool saveCurrentAs(const QString& filePath, QString* errorMessage = nullptr);
    bool reopenCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage = nullptr);
    bool saveDocument(core::DocumentId documentId, QString* errorMessage = nullptr, bool force = false);
    bool saveDocumentAs(core::DocumentId documentId, const QString& filePath, QString* errorMessage = nullptr);
    bool reloadDocument(core::DocumentId documentId, QString* errorMessage = nullptr);
    bool closeDocument(core::DocumentId documentId);
    void markDocumentDeleted(core::DocumentId documentId);
    void resumeWatching(core::DocumentId documentId);
    void rebindPaths(const QString& previousPath, const QString& path);
    [[nodiscard]] QVector<EditorDocumentInfo> documents() const;
    [[nodiscard]] editor::DocumentModelStore& documentModels() { return documentModels_; }
    [[nodiscard]] bool hasDocumentEditor(core::DocumentId id) const { return pageForDocument(id) != nullptr; }
    [[nodiscard]] core::TextEncoding documentDefaultEncoding() const { return defaultEncoding_; }
    [[nodiscard]] bool documentAutoGuessEncoding() const { return autoGuessEncoding_; }
    // Workbench-only entry point; extension-facing APIs live in the SDK/host.
    bool acceptDocumentLoad(core::DocumentId id, const editor::DocumentLoadResult& load,
                            QString* errorMessage = nullptr);
    void announceDocument(core::DocumentId id);
    void releaseDocumentReference(core::DocumentId id);
    void disposeUnusedDocument(core::DocumentId id);
    bool applyVirtualDocumentEdit(core::DocumentId id, core::DocumentEdit edit);
    bool applyVirtualDocumentEdits(core::DocumentId id, qint64 expectedVersion, QVector<core::DocumentEdit> edits);
    bool applyDocumentEditPlans(const QVector<DocumentEditPlan>& plans, QString* error = nullptr);
    bool applyTextEditorEdit(core::DocumentId id, const extensions::TextEditorEditData& edit, QString* error = nullptr);
    bool applyTextEditorEdit(const QString& editorId, const extensions::TextEditorEditData& edit, QString* error = nullptr);
    [[nodiscard]] extensions::WindowEditorState extensionEditorState() const;
    [[nodiscard]] std::optional<extensions::TextDocumentSnapshot> extensionDocumentSnapshot(core::DocumentId id) const;
    [[nodiscard]] std::optional<extensions::TextDocumentMetadata> extensionDocumentMetadata(core::DocumentId id) const;
    [[nodiscard]] QStringList openFiles() const;
    [[nodiscard]] QString currentFile() const;
    [[nodiscard]] core::TextEncoding currentEncoding() const;
    [[nodiscard]] core::LineEnding currentLineEnding() const;
    [[nodiscard]] QString currentLanguageId() const;
    [[nodiscard]] QString selectedText() const;
    [[nodiscard]] std::optional<FormatDocumentSnapshot> currentFormatSnapshot() const;
    bool applyFormattedDocument(const FormatDocumentSnapshot& snapshot, const QString& text);
    void goToLine(int line);
    void goToMatch(int line, int column, int length);
    void selectAll();
    void undo();
    void redo();
    void setWorkspaceRoot(const QString& rootPath);
    void setHistorySettings(core::SettingsService* settings);
    void showFindReplace(bool replaceVisible = true);
    void setEditorFont(const QString& family, int pixelSize, int weight = QFont::Normal);
    void resetEditorZoom();
    void setIndentWidth(int width);
    void setDefaultEncoding(core::TextEncoding encoding);
    void setAutoGuessEncoding(bool enabled);
    void setDarkTheme(bool dark);
    void setGitDecorationModel(ExplorerFileSystemModel& model);
    void setBreakpointLanguages(const QSet<QString>& languages);
    void setDebugBreakpoints(const QMap<QString, QSet<int>>& breakpoints);
    void setDebugExecutionLocation(const QString& path, int line);

  signals:
    void currentFileChanged(const QString& filePath);
    void currentEncodingChanged(litecode::core::TextEncoding encoding);
    void openFilesChanged();
    void documentOpened(litecode::core::DocumentId documentId, const QString& filePath,
                        const QByteArray& contents, qint64 version,
                        litecode::core::DocumentStorageMode storageMode);
    void documentEdited(const litecode::core::DocumentEdit& edit);
    void documentEditsApplied(litecode::core::DocumentId id, const QVector<litecode::core::DocumentEdit>& edits);
    void documentSnapshotChanged(litecode::core::DocumentId id, litecode::core::DocumentEditReason reason);
    void documentSnapshotApplied(const litecode::extensions::TextDocumentSnapshot& snapshot);
    void extensionEditorStateChanged(const litecode::extensions::WindowEditorState& state);
    void documentMetadataChanged(litecode::core::DocumentId id);
    void documentSaved(litecode::core::DocumentId id);
    void documentPathChanged(const litecode::core::DocumentPathChange& change);
    void documentResourceRemoved(const QUrl& resource);
    void documentClosed(litecode::core::DocumentId documentId, const QString& filePath,
                        qint64 finalVersion);
    void cursorPositionChanged(int line, int column);
    void documentCloseRequested(litecode::core::DocumentId documentId);
    void revealFileRequested(const QString& filePath);
    void revealInFileExplorerRequested(const QString& filePath);
    void externalModificationDetected(const litecode::ui::ExternalDocumentChange& change);
    void fileOpenFailed(const QString& filePath, const QString& diagnostic);
    void fileReloadFailed(const QString& filePath, const QString& diagnostic);
    void largeFileModeActivated(const QString& filePath);
    void breakpointToggleRequested(const QString& filePath, int line);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    void openComparison(const workspace::GitComparison& comparison, const QString& key,
                        const QString& titleOverride = {}, bool preserveFocus = false, bool replaceExisting = true, bool openFileAvailable = true);

    struct Page;

    [[nodiscard]] Page* pageAt(int index) const;
    [[nodiscard]] Page* pageForDocument(core::DocumentId documentId) const;
    [[nodiscard]] int indexOfPath(const QString& filePath) const;
    void finishOpen(core::DocumentId documentId, const editor::DocumentLoadResult& load);
    bool finishReload(Page& page, const editor::DocumentLoadResult& load, QString* errorMessage);
    void initializeEditor(Page& page, const QByteArray& contents);
    void scheduleEditorStateChanged();
    void updateTabTitle(Page& page);
    void updateTabDecoration(const Page& page, int index);
    bool closePage(int index);
    void findNext();
    void findPrevious();
    void handleFindInputChanged();
    void scheduleFindRefresh(bool moveCursor = false);
    void refreshFindCount();
    void updateFindResult(bool found);
    void closeFindReplace();
    void replaceNext();
    void replaceAll();
    void setReplaceVisible(bool visible);
    void updateFindBarGeometry();
    void rememberFindHistory(components::FindTextInput* input, QStringList& history);
    bool cycleFindHistory(components::FindTextInput* input, QStringList& history, int& index,
                          QString& draft, bool older);
    void handleExternalChange(const QString& filePath);
    void confirmExternalDeletion(core::DocumentId documentId, const QString& filePath);
    void scheduleDeletionConfirmation(core::DocumentId documentId, const QString& filePath);
    void watch(Page& page);
    void updateBreadcrumb(Page& page);
    void updateTabCloseButtons(int hoveredTab = -1);
    void updateTabOverflowScrollBar();
    void showTabContextMenu(int index, const QPoint& globalPosition);
    void scheduleAutomaticReload(const Page& page);
    void reloadPendingExternalChanges();
    void markExternallyDeleted(Page& page);
    void showReloadError(Page& page, const QString& message);
    void handleExternalDirectoryChange();
    void refreshDeletedDirectoryWatches();

    components::WorkbenchTabs* tabs_{};
    std::optional<QSet<QString>> breakpointLanguages_;
    QMap<QString, QSet<int>> debugBreakpoints_;
    QString debugExecutionPath_;
    int debugExecutionLine_{};
    QStackedWidget* editorStack_{};
    QWidget* welcomePage_{};
    QFrame* findBar_{};
    components::FindTextInput* findInput_{};
    components::FindTextInput* replaceInput_{};
    QFrame* findInputFrame_{};
    QLabel* findResultLabel_{};
    QToolButton* replaceToggle_{};
    QToolButton* matchCase_{};
    QToolButton* wholeWord_{};
    QToolButton* regularExpression_{};
    QToolButton* findSelectionButton_{};
    QToolButton* replaceButton_{};
    QToolButton* replaceAllButton_{};
    components::EditorFindReplaceController* findReplaceController_{};
    bool replaceVisible_{};
    QPair<qint64, qint64> findSelectionRange_{};
    QFileSystemWatcher* watcher_{};
    QTimer* externalReloadTimer_{};
    QTimer* findCountTimer_{};
    QHash<quint64, qint64> pendingExternalReloads_;
    quint64 findCountGeneration_{};
    bool findMoveCursor_{};
    QPair<qint64, qint64> findSelectionAtRequest_;
    QString largeFindNeedle_;
    int largeFindTotal_{};
    bool largeFindLimitHit_{};
    bool largeFindMatchCase_{};
    bool largeFindWholeWord_{};
    bool largeFindRegularExpression_{};
    QString fontFamily_{QStringLiteral("Consolas")};
    int fontPixelSize_{14};
    int fontWeight_{QFont::Normal};
    int indentWidth_{4};
    core::TextEncoding defaultEncoding_{core::TextEncoding::Utf8};
    bool autoGuessEncoding_{};
    bool darkTheme_{true};
    bool editorStateUpdatePending_{};
    QString workspaceRoot_;
    core::SettingsService* historySettings_{};
    QStringList findHistory_;
    QStringList replaceHistory_;
    int findHistoryIndex_{-1};
    int replaceHistoryIndex_{-1};
    QString findHistoryDraft_;
    QString replaceHistoryDraft_;
    quint64 workspaceGeneration_{1};
    core::DocumentIdGenerator documentIds_;
    editor::DocumentModelStore documentModels_{documentIds_};
    std::vector<std::unique_ptr<Page>> pages_;
    QPointer<ExplorerFileSystemModel> gitDecorationModel_;
};

} // namespace litecode::ui

Q_DECLARE_METATYPE(litecode::ui::EditorDocumentInfo)
Q_DECLARE_METATYPE(litecode::ui::ExternalDocumentChange)
