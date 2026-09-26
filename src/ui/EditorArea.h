#pragma once

#include "core/DocumentTypes.h"

#include <QFont>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <QHash>

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
namespace litecode::core {
class SettingsService;
}

namespace litecode::ui {

namespace components {
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

class EditorArea final : public QWidget {
    Q_OBJECT

  public:
    explicit EditorArea(QWidget* parent = nullptr);
    ~EditorArea() override;

    bool openFile(const QString& filePath, QString* errorMessage = nullptr,
                  std::optional<core::TextEncoding> requestedEncoding = std::nullopt);
    bool saveCurrent(QString* errorMessage = nullptr);
    bool saveCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage = nullptr);
    bool saveCurrentAs(const QString& filePath, QString* errorMessage = nullptr);
    bool reopenCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage = nullptr);
    bool saveDocument(core::DocumentId documentId, QString* errorMessage = nullptr);
    bool reloadDocument(core::DocumentId documentId, QString* errorMessage = nullptr);
    bool closeDocument(core::DocumentId documentId);
    void markDocumentDeleted(core::DocumentId documentId);
    void resumeWatching(core::DocumentId documentId);
    void rebindPaths(const QString& previousPath, const QString& path);
    [[nodiscard]] QVector<EditorDocumentInfo> documents() const;
    [[nodiscard]] QStringList openFiles() const;
    [[nodiscard]] QString currentFile() const;
    [[nodiscard]] core::TextEncoding currentEncoding() const;
    [[nodiscard]] QString selectedText() const;
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

  signals:
    void currentFileChanged(const QString& filePath);
    void currentEncodingChanged(litecode::core::TextEncoding encoding);
    void openFilesChanged();
    void documentOpened(litecode::core::DocumentId documentId, const QString& filePath,
                        const QByteArray& contents, qint64 version,
                        litecode::core::DocumentStorageMode storageMode);
    void documentEdited(const litecode::core::DocumentEdit& edit);
    void documentPathChanged(const litecode::core::DocumentPathChange& change);
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

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    struct Page;

    [[nodiscard]] Page* pageAt(int index) const;
    [[nodiscard]] Page* pageForDocument(core::DocumentId documentId) const;
    [[nodiscard]] int indexOfPath(const QString& filePath) const;
    void finishOpen(core::DocumentId documentId, const editor::DocumentLoadResult& load);
    bool finishReload(Page& page, const editor::DocumentLoadResult& load, QString* errorMessage);
    void initializeEditor(Page& page, const QByteArray& contents);
    void updateTabTitle(Page& page);
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
    void cacheTabScrollButtons();
    void showTabContextMenu(int index, const QPoint& globalPosition);
    void scheduleAutomaticReload(const Page& page);
    void reloadPendingExternalChanges();
    void markExternallyDeleted(Page& page);
    void showReloadError(Page& page, const QString& message);
    void handleExternalDirectoryChange();
    void refreshDeletedDirectoryWatches();

    QTabWidget* tabs_{};
    QScrollBar* tabOverflowScroll_{};
    QToolButton* tabScrollLeft_{};
    QToolButton* tabScrollRight_{};
    int tabOverflowPosition_{};
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
    std::vector<std::unique_ptr<Page>> pages_;
};

} // namespace litecode::ui

Q_DECLARE_METATYPE(litecode::ui::EditorDocumentInfo)
Q_DECLARE_METATYPE(litecode::ui::ExternalDocumentChange)
