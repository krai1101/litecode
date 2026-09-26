#pragma once

#include "core/DocumentTypes.h"
#include "workspace/WorkspaceSearch.h"
#include <QHash>
#include <QMainWindow>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QVector>

#include <optional>

class QAction;
class QDockWidget;
class QEvent;
class QStackedWidget;
class QTreeView;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QMenuBar;
class QTimer;
class QToolBar;
class QLabel;
class QToolButton;
class QWidget;
class QTabWidget;
class QShowEvent;

namespace litecode::core {
class SettingsService;
}

namespace litecode::workspace {
class WorkspaceService;
class WorkspaceSearch;
class WorkspaceReplace;
struct SearchResult;
} // namespace litecode::workspace

namespace litecode::ui {

class EditorArea;
class KeyboardShortcutsDialog;
class SettingsDialog;
class TerminalPanel;
class ExplorerTreeView;
class ExplorerFileSystemModel;
class ExplorerController;
class EditorSessionController;
class WorkbenchController;
class WorkbenchTitleBar;
class WindowOutline;
class CommandRegistry;
struct ExplorerEditRequest;

struct WorkbenchServices final {
    workspace::WorkspaceSearch& workspaceSearch;
    workspace::WorkspaceSearch& quickOpenSearch;
    workspace::WorkspaceReplace& workspaceReplace;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(core::SettingsService& settings, workspace::WorkspaceService& workspace,
                        WorkbenchServices services, bool restorePreviousSession,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

  signals:
    void firstFramePresented();

  protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

  private:
    enum class DockResizeAxis { None, Horizontal, Vertical };
    struct UserSettingsSnapshot {
        QString fontFamily;
        int fontSize{};
        int fontWeight{};
        bool autoGuess{};
        core::TextEncoding encoding{core::TextEncoding::Utf8};
        int indentWidth{};
        QString terminalFontFamily;
        int terminalFontSize{};
        QString terminalProfile;
        QString theme;
        QHash<QString, QString> shortcuts;
        bool operator==(const UserSettingsSnapshot&) const = default;
    };

    void buildActions();
    void buildNavigationBar();
    void buildActivityBar();
    void buildWorkspaceDock();
    void buildEditorShell();
    void buildBottomPanel();
    void buildStatusBar();
    void toggleTerminalPanel();
    void setBottomPanelMaximized(bool maximized);
    void restoreLayout();
    void applySavedWorkbenchLayout();
    void restoreSession();
    void enableSessionPersistence();
    void saveLayout();
    void chooseWorkspaceFolder();
    void applyWorkspace(const QString& rootPath);
    void revealActiveFile();
    void revealInFileExplorer(const QString& path);
    void copyActiveFilePath();
    void openTreeEntry(const QModelIndex& index);
    void showFileTreeMenu(const QPoint& position);
    void setExplorerRootSelected(bool selected);
    void requestExplorerDelete();
    void showExplorerEdit(const ExplorerEditRequest& request);
    void hideExplorerCreateRow();
    void updateExplorerExclusions();
    void saveCurrentFile();
    void saveCurrentFileAs();
    void showEncodingMenu();
    [[nodiscard]] std::optional<core::TextEncoding>
    chooseTextEncoding(const QString& filePath, const QString& placeholder,
                       const QVector<core::TextEncoding>& encodings,
                       std::optional<core::TextEncoding> currentEncoding, bool suggestFromContent);
    void reopenCurrentWithEncoding(core::TextEncoding encoding);
    void saveCurrentWithEncoding(core::TextEncoding encoding);
    void updateEncodingStatus(core::TextEncoding encoding);
    void configureSettings();
    void checkExternalUserSettings();
    [[nodiscard]] UserSettingsSnapshot captureUserSettings() const;
    void applyEditorSettings();
    void refreshSettingsDialogValues();
    bool persistSettings(bool showWarning = true);
    void setEditorIndentWidth(int width);
    void chooseTerminalFont();
    void configureKeyboardShortcuts();
    void applyTheme(const QString& theme);
    void applyThemeVisuals(const QString& theme);
    void updateThemeIcons(bool dark);
    void chooseFile();
    void chooseFileWithEncoding();
    void runWorkspaceSearch();
    void invalidateWorkspaceSearch(bool clearVisibleResults = false);
    void refreshWorkspaceSearch();
    void replaceWorkspaceMatches(const QString& targetPath = {}, int targetLine = 0,
                                 int targetColumn = 0, int targetLength = 0);
    void displaySearchResults(const QVector<workspace::SearchResult>& results);
    void toggleSearchResultFile(QListWidgetItem* item);
    core::SettingsService& settings_;
    WorkbenchController* workbenchController_{};
    QDockWidget* workspaceDock_{};
    QDockWidget* bottomDock_{};
    bool bottomPanelMaximized_{false};
    int bottomPanelRestoreHeight_{};
    WorkbenchTitleBar* titleBar_{};
    WindowOutline* windowOutline_{};
    QMenuBar* workbenchMenuBar_{};
    QToolBar* activityBar_{};
    QLabel* workspaceTitle_{};
    QWidget* workspaceRootBar_{};
    QToolButton* workspaceRootLabel_{};
    bool showGeneratedFolders_{false};
    QStackedWidget* workspacePages_{};
    ExplorerFileSystemModel* fileSystemModel_{};
    ExplorerTreeView* fileTree_{};
    QPersistentModelIndex explorerPressedIndex_;
    bool explorerExpandedAtPress_{false};
    ExplorerController* explorerController_{};
    QWidget* searchHeaderActions_{};
    QLineEdit* searchInput_{};
    QLineEdit* replaceInput_{};
    QWidget* replaceRow_{};
    QToolButton* replaceToggle_{};
    QToolButton* matchCaseButton_{};
    QToolButton* matchWholeWordButton_{};
    QToolButton* useRegexButton_{};
    QToolButton* preserveCaseButton_{};
    QListWidget* searchResults_{};
    QTimer* searchTimer_{};
    QStringList searchHistory_;
    QStringList currentSearchResultPaths_;
    QVector<workspace::SearchResult> currentSearchResults_;
    QSet<QString> collapsedSearchFiles_;
    quint64 activeSearchOperationId_{};
    bool searchResultsReady_{};
    bool searchResultsTruncated_{};
    int searchHistoryIndex_{-1};
    workspace::WorkspaceSearch* workspaceSearch_{};
    workspace::WorkspaceSearch* quickOpenSearch_{};
    workspace::WorkspaceReplace* workspaceReplace_{};
    QToolButton* replaceAllWorkspaceButton_{};
    EditorSessionController* editorSessionController_{};
    QTabWidget* bottomTabs_{};
    QString workspaceSearchRoot_;
    QLabel* cursorStatus_{};
    QToolButton* encodingStatusButton_{};
    QToolButton* indentStatusButton_{};
    EditorArea* editorArea_{};
    TerminalPanel* terminalPanel_{};
    CommandRegistry* commands_{};
    KeyboardShortcutsDialog* keyboardShortcutsDialog_{};
    SettingsDialog* settingsDialog_{};
    QTimer* userSettingsPollTimer_{};
    UserSettingsSnapshot userSettingsSnapshot_;
    bool userEncodingErrorShown_{};
    bool settingsDialogChanged_{};
    bool settingsWriteWarningShown_{};
    QAction* explorerAction_{};
    QAction* searchAction_{};
    QAction* manageAction_{};
    bool closing_{false};
    bool restoringSession_{true};
    bool applyingSavedLayout_{false};
    bool postShowLayoutApplied_{false};
    bool firstFrameSeen_{false};
    DockResizeAxis dockResizeAxis_{DockResizeAxis::None};
    QPoint dockResizePressPosition_;
    int dockResizeInitialSize_{};
    QWidget* dockResizeCursorTarget_{};
};

} // namespace litecode::ui
