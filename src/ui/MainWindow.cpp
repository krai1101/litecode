#include "ui/MainWindow.h"

#include "core/SettingsService.h"
#include "core/TextEncoding.h"
#include "editor/SyntaxTheme.h"
#include "ui/ConfirmationDialog.h"
#include "ui/EditorArea.h"
#include "ui/EditorFontDialog.h"
#include "ui/FileIconTheme.h"
#include "ui/KeyboardShortcutsDialog.h"
#include "ui/PopupPositioner.h"
#include "ui/QuickOpenDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/TerminalPanel.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/TransientScrollBars.h"
#include "ui/WindowOutline.h"
#include "ui/WorkbenchChrome.h"
#include "ui/WorkbenchDock.h"
#include "ui/WorkbenchTitleBar.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/components/ComponentTokens.h"
#include "ui/components/Controls.h"
#include "ui/controllers/EditorSessionController.h"
#include "ui/controllers/ExplorerController.h"
#include "ui/controllers/WorkbenchController.h"
#include "ui/explorer/ExplorerTreeView.h"
#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"

#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QShortcut>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <algorithm>
#include <functional>
#include <utility>

namespace litecode::ui {
namespace {

// Bump this whenever the dock topology or minimum dimensions change. Qt's saved dock state
// otherwise preserves widths from older builds and can collapse a newly polished sidebar.
constexpr int workbenchLayoutVersion = 5;
constexpr int searchResultIndexRole = Qt::UserRole + 5;

class SearchResultsDelegate final : public QStyledItemDelegate {
  public:
    explicit SearchResultsDelegate(const QVector<workspace::SearchResult>* results, QObject* parent)
        : QStyledItemDelegate(parent), results_(results) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (index.data(Qt::UserRole + 2).toString() == QStringLiteral("workspaceSearchWarning")) {
            const int width = qMax(80, option.widget->width() - 36);
            const QFontMetrics metrics(option.font);
            const QRect text =
                metrics.boundingRect(QRect(0, 0, width, 4096), Qt::AlignLeft | Qt::TextWordWrap,
                                     index.data().toString());
            // Allow for the drawing insets and fractional display scaling without
            // wasting another full text line below the warning.
            return {0, qMax(30, text.height() + 14)};
        }
        return {0, 22};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QString kind = index.data(Qt::UserRole + 2).toString();
        if (kind == QStringLiteral("workspaceSearchWarning")) {
            QStyleOptionViewItem background(option);
            initStyleOption(&background, index);
            background.text.clear();
            option.widget->style()->drawControl(QStyle::CE_ItemViewItem, &background, painter,
                                                option.widget);
            const ThemeTokens colors = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
            static const QIcon lightWarning =
                tintedIcon(QStringLiteral(":/icons/warning.svg"), Theme::tokens(false).warning,
                           Theme::tokens(false).warning);
            static const QIcon darkWarning =
                tintedIcon(QStringLiteral(":/icons/warning.svg"), Theme::tokens(true).warning,
                           Theme::tokens(true).warning);
            painter->save();
            painter->setClipRect(option.rect);
            (qApp->property("litecodeDarkTheme").toBool() ? darkWarning : lightWarning)
                .paint(painter, QRect(option.rect.left() + 6, option.rect.top() + 3, 16, 16));
            painter->setPen(colors.mutedForeground);
            painter->drawText(option.rect.adjusted(28, 3, -6, -3), Qt::TextWordWrap,
                              index.data().toString());
            painter->restore();
            return;
        }
        if (kind != QStringLiteral("workspaceSearchFileHeader") &&
            kind != QStringLiteral("workspaceSearchMatch")) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        background.icon = {};
        option.widget->style()->drawControl(QStyle::CE_ItemViewItem, &background, painter,
                                            option.widget);

        const bool dark = qApp->property("litecodeDarkTheme").toBool();
        const ThemeTokens colors = Theme::tokens(dark);
        const QFontMetrics metrics(option.font);
        const int baseline =
            option.rect.top() + (option.rect.height() + metrics.ascent() - metrics.descent()) / 2;
        const int actionWidth =
            option.widget->property("litecodeSearchReplaceVisible").toBool() ? 24 : 0;
        painter->save();
        painter->setClipRect(option.rect);
        if (kind == QStringLiteral("workspaceSearchFileHeader")) {
            const QString path = index.data(Qt::UserRole).toString();
            const bool collapsed = index.data(Qt::UserRole + 6).toBool();
            painter->setPen(colors.iconForeground);
            const int chevronX = option.rect.left() + 8;
            const int centerY = option.rect.center().y();
            if (collapsed) {
                painter->drawLine(chevronX, centerY - 4, chevronX + 4, centerY);
                painter->drawLine(chevronX + 4, centerY, chevronX, centerY + 4);
            } else {
                painter->drawLine(chevronX - 2, centerY - 2, chevronX + 2, centerY + 2);
                painter->drawLine(chevronX + 2, centerY + 2, chevronX + 6, centerY - 2);
            }
            fileIconForPath(path).paint(
                painter, QRect(option.rect.left() + 21, option.rect.top() + 3, 16, 16));
            const int nameX = option.rect.left() + 41;
            const QString name = QFileInfo(path).fileName();
            const QString count = index.data(Qt::UserRole + 7).toString();
            const int countWidth = metrics.horizontalAdvance(count);
            const int countX = option.rect.right() - actionWidth - countWidth - 8;
            const int textRight = countX - 10;
            const int availableWidth = qMax(0, textRight - nameX);
            const QString directory = index.data(Qt::UserRole + 8).toString();
            int nameWidth = availableWidth;
            if (!directory.isEmpty() && availableWidth > 70) {
                const int desiredPathWidth = metrics.horizontalAdvance(directory);
                const int pathReservation = qMin(desiredPathWidth, qMax(36, availableWidth / 3));
                nameWidth = qMax(24, availableWidth - pathReservation - 8);
            }
            const QString visibleName = metrics.elidedText(name, Qt::ElideRight, nameWidth);
            painter->setPen(colors.foreground);
            painter->drawText(QRect(nameX, option.rect.top(), nameWidth, option.rect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, visibleName);
            painter->setPen(colors.mutedForeground);
            painter->drawText(QRect(countX, option.rect.top(), countWidth, option.rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter, count);
            if (!directory.isEmpty()) {
                const int pathX = nameX + metrics.horizontalAdvance(visibleName) + 8;
                const int pathWidth = qMax(0, textRight - pathX);
                const QString visiblePath =
                    metrics.elidedText(directory, Qt::ElideRight, pathWidth);
                painter->drawText(QRect(pathX, option.rect.top(), pathWidth, option.rect.height()),
                                  Qt::AlignLeft | Qt::AlignVCenter, visiblePath);
            }
        } else {
            const int resultIndex = index.data(searchResultIndexRole).toInt();
            if (resultIndex >= 0 && resultIndex < results_->size()) {
                const workspace::SearchResult& result = results_->at(resultIndex);
                const int lineX = option.rect.left() + 29;
                const QString line = QStringLiteral("%1:").arg(result.line);
                painter->setPen(colors.mutedForeground);
                painter->drawText(lineX, baseline, line);
                const int previewX = lineX + metrics.horizontalAdvance(line) + 6;
                painter->setClipRect(QRect(previewX, option.rect.top(),
                                           qMax(0, option.rect.right() - actionWidth - previewX),
                                           option.rect.height()));
                painter->setPen(colors.foreground);
                const int available = qMax(0, option.rect.right() - actionWidth - previewX);
                QString preview = result.preview;
                // Browser-based VS Code uses CSS text-overflow: ellipsis. Reproduce
                // that at the actual viewport width before QTextLayout paints it.
                if (metrics.horizontalAdvance(preview) > available) {
                    int visibleCharacters = 0;
                    while (visibleCharacters < preview.size() &&
                           metrics.horizontalAdvance(preview.left(visibleCharacters + 1) +
                                                     QChar(0x2026)) <= available) {
                        ++visibleCharacters;
                    }
                    preview = preview.left(visibleCharacters) + QChar(0x2026);
                }
                const int matchStart = result.previewMatchStart;
                if (matchStart < 0 || matchStart >= preview.size() ||
                    result.matchedText.isEmpty()) {
                    painter->drawText(previewX, baseline, preview);
                } else {
                    const QColor highlight = editor::SyntaxTheme::palette(dark).findMatchHighlight;
                    const int alpha = highlight.alpha();
                    const QColor surface = colors.sidebarSurface;
                    const QColor opaqueHighlight(
                        (highlight.red() * alpha + surface.red() * (255 - alpha)) / 255,
                        (highlight.green() * alpha + surface.green() * (255 - alpha)) / 255,
                        (highlight.blue() * alpha + surface.blue() * (255 - alpha)) / 255);
                    QTextLayout textLayout(preview, option.font);
                    QTextOption textOption;
                    textOption.setWrapMode(QTextOption::NoWrap);
                    // Source previews retain leading indentation. Use code-editor tab stops
                    // rather than QTextLayout's much wider platform default, otherwise a few
                    // leading tabs can push the matched text outside the results pane.
                    textOption.setTabStopDistance(static_cast<qreal>(
                        qMax(1, metrics.horizontalAdvance(QLatin1Char(' ')) * 4)));
                    textLayout.setTextOption(textOption);
                    QTextLayout::FormatRange matchFormat;
                    matchFormat.start = matchStart;
                    matchFormat.length =
                        qMin(result.matchedText.size(), preview.size() - matchStart);
                    matchFormat.format.setBackground(opaqueHighlight);
                    matchFormat.format.setForeground(colors.foreground);
                    textLayout.setFormats({matchFormat});
                    textLayout.beginLayout();
                    QTextLine textLine = textLayout.createLine();
                    textLine.setLineWidth(available);
                    textLayout.endLayout();
                    const qreal top =
                        option.rect.top() + (option.rect.height() - textLine.height()) / 2.0;
                    textLayout.draw(painter, QPointF(previewX, top));
                }
            }
        }
        if (actionWidth > 0 && option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setClipping(false);
            static const QIcon lightReplace =
                themedIcon(QStringLiteral(":/icons/replace.svg"), false);
            static const QIcon darkReplace =
                themedIcon(QStringLiteral(":/icons/replace.svg"), true);
            static const QIcon lightReplaceAll =
                themedIcon(QStringLiteral(":/icons/replace-all.svg"), false);
            static const QIcon darkReplaceAll =
                themedIcon(QStringLiteral(":/icons/replace-all.svg"), true);
            const QIcon& actionIcon = kind == QStringLiteral("workspaceSearchFileHeader")
                                          ? (dark ? darkReplaceAll : lightReplaceAll)
                                          : (dark ? darkReplace : lightReplace);
            actionIcon.paint(painter,
                             QRect(option.rect.right() - 20, option.rect.top() + 3, 16, 16));
        }
        painter->restore();
    }

  private:
    const QVector<workspace::SearchResult>* results_;
};

Qt::KeyboardModifiers primaryModifiers(Qt::KeyboardModifiers additional = {}) {
#ifdef Q_OS_MACOS
    return Qt::MetaModifier | additional;
#else
    return Qt::ControlModifier | additional;
#endif
}

QKeySequence primaryShortcut(Qt::Key key, Qt::KeyboardModifiers additional = {}) {
    return QKeySequence(QKeyCombination(primaryModifiers(additional), key));
}

QKeySequence primaryChord(Qt::Key first, Qt::Key second) {
    return QKeySequence(QKeyCombination(primaryModifiers(), first),
                        QKeyCombination(primaryModifiers(), second));
}

bool isSameFilePath(const QString& left, const QString& right) {
    const auto resolvedPath = [](const QString& path) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    };
#ifdef Q_OS_LINUX
    constexpr auto sensitivity = Qt::CaseSensitive;
#else
    constexpr auto sensitivity = Qt::CaseInsensitive;
#endif
    return resolvedPath(left).compare(resolvedPath(right), sensitivity) == 0;
}

} // namespace

MainWindow::MainWindow(core::SettingsService& settings, workspace::WorkspaceService& workspace,
                       WorkbenchServices services, bool restorePreviousSession, QWidget* parent)
    : QMainWindow(parent), settings_(settings), workspaceSearch_(&services.workspaceSearch),
      quickOpenSearch_(&services.quickOpenSearch), workspaceReplace_(&services.workspaceReplace) {
    qApp->setProperty("litecodeDarkTheme", settings_.theme() != QStringLiteral("light"));
    setStyle(sharedWorkbenchStyle());
    qApp->installEventFilter(this);
    setObjectName(QStringLiteral("mainWindow"));
    setContentsMargins(0, 0, 0, 0);
    setWindowTitle(QStringLiteral("LiteCode"));
#ifdef Q_OS_WIN
    setWindowFlag(Qt::FramelessWindowHint);
#endif
    setDockNestingEnabled(true);
    setAnimated(false);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    commands_ = new CommandRegistry(this);
    explorerController_ = new ExplorerController(workspace, this);
    workbenchController_ = new WorkbenchController(this);
    editorArea_ = new EditorArea(this);
    editorArea_->setHistorySettings(&settings_);
    editorSessionController_ = new EditorSessionController(*editorArea_, this);
    workbenchMenuBar_ = new QMenuBar(this);
    buildActions();
    buildNavigationBar();
    buildActivityBar();
    buildWorkspaceDock();
    buildEditorShell();
    buildBottomPanel();
    buildStatusBar();
    userSettingsSnapshot_ = captureUserSettings();
    userSettingsPollTimer_ = new QTimer(this);
    userSettingsPollTimer_->setInterval(2000);
    connect(userSettingsPollTimer_, &QTimer::timeout, this, &MainWindow::checkExternalUserSettings);
    userSettingsPollTimer_->start();
    QTimer::singleShot(0, this, &MainWindow::checkExternalUserSettings);
    updateThemeIcons(settings_.theme() != QStringLiteral("light"));
    restoreLayout();
    if (restorePreviousSession)
        restoreSession();
    restoringSession_ = false;
    enableSessionPersistence();
    windowOutline_ = new WindowOutline(this);
    windowOutline_->setDarkTheme(settings_.theme() != QStringLiteral("light"));
    windowOutline_->syncToWindow(isMaximized());
#ifdef Q_OS_WIN
    // FramelessWindowHint removes the native sizing frame. Restore that capability while
    // continuing to paint and hit-test LiteCode's own title bar.
    const HWND nativeWindow = reinterpret_cast<HWND>(winId());
    const LONG_PTR nativeStyle = GetWindowLongPtr(nativeWindow, GWL_STYLE);
    SetWindowLongPtr(nativeWindow, GWL_STYLE,
                     nativeStyle | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowPos(nativeWindow, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#endif
}

MainWindow::~MainWindow() {
    closing_ = true;
    qApp->removeEventFilter(this);
    if (workspaceDock_)
        disconnect(workspaceDock_, nullptr, this, nullptr);
    if (bottomDock_)
        disconnect(bottomDock_, nullptr, this, nullptr);
    // Child services may finish processes synchronously during QObject child teardown. Stop
    // delivering their UI callbacks before QMainWindow begins deleting the status bar and
    // other child widgets.
    if (workspaceSearch_) {
        workspaceSearch_->cancel();
        disconnect(workspaceSearch_, nullptr, this, nullptr);
    }
    if (quickOpenSearch_) {
        quickOpenSearch_->cancel();
        disconnect(quickOpenSearch_, nullptr, this, nullptr);
    }
    if (workspaceReplace_) {
        workspaceReplace_->cancel();
        disconnect(workspaceReplace_, nullptr, this, nullptr);
    }
}

void MainWindow::buildActions() {
    bool normalizedStoredShortcut = false;
    const auto storedShortcut = [this](const QString& id) -> std::optional<QKeySequence> {
        if (!settings_.hasCommandShortcut(id))
            return std::nullopt;
        const QString stored = settings_.commandShortcut(id, {});
        QKeySequence sequence = QKeySequence::fromString(stored, QKeySequence::PortableText);
        if (!stored.isEmpty() && sequence.isEmpty())
            sequence = QKeySequence::fromString(stored, QKeySequence::NativeText);
        return sequence;
    };
    const auto addCommand = [this, &storedShortcut, &normalizedStoredShortcut](CommandSpec spec) {
        const QString id = spec.id;
        const std::optional<QKeySequence> current = storedShortcut(spec.id);
        QAction* action = commands_->registerCommand(std::move(spec), current);
        if (action != nullptr && current.has_value() && action->shortcut() != current.value()) {
            settings_.saveCommandShortcut(id,
                                          action->shortcut().toString(QKeySequence::PortableText));
            normalizedStoredShortcut = true;
        }
        return action;
    };

    auto* fileMenu = new components::Menu(tr("File"), workbenchMenuBar_);
    auto* newWindow =
        addCommand({QStringLiteral("file.newWindow"), tr("New Window"), tr("File: New Window"),
                    primaryShortcut(Qt::Key_N, Qt::ShiftModifier),
                    CommandSurface::Menu | CommandSurface::ShortcutEditor, [this] {
                        if (!QProcess::startDetached(QApplication::applicationFilePath(),
                                                     {QStringLiteral("--new-window")})) {
                            statusBar()->showMessage(tr("Could not open a new window."), 4000);
                        }
                    }});
    newWindow->setObjectName(QStringLiteral("newWindowAction"));
    fileMenu->addAction(newWindow);
    fileMenu->addSeparator();
    auto* openFolder = addCommand({QStringLiteral("file.openFolder"), tr("Open Folder…"),
                                   tr("File: Open Folder"), primaryChord(Qt::Key_K, Qt::Key_O),
                                   CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                   [this] { chooseWorkspaceFolder(); }});
    openFolder->setObjectName(QStringLiteral("openFolderAction"));
    fileMenu->addAction(openFolder);
    fileMenu->addAction(addCommand({QStringLiteral("file.openFile"), tr("Open File…"),
                                    tr("File: Open File"), QKeySequence(QKeySequence::Open),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [this] { chooseFile(); }}));
    addCommand({QStringLiteral("file.openFileWithEncoding"),
                tr("Open File with Encoding…"),
                tr("File: Open File with Encoding"),
                {},
                CommandSurface::Palette,
                [this] { chooseFileWithEncoding(); }});
    fileMenu->addSeparator();
    fileMenu->addAction(addCommand({QStringLiteral("file.save"), tr("Save"), tr("File: Save"),
                                    QKeySequence(QKeySequence::Save),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [this] { saveCurrentFile(); }}));
    fileMenu->addAction(addCommand({QStringLiteral("file.saveAs"), tr("Save As…"),
                                    tr("File: Save As"), QKeySequence(QKeySequence::SaveAs),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [this] { saveCurrentFileAs(); }}));
    addCommand({QStringLiteral("file.changeEncoding"),
                tr("Change File Encoding…"),
                tr("File: Change File Encoding"),
                {},
                CommandSurface::Palette,
                [this] { showEncodingMenu(); }});
    fileMenu->addSeparator();
    fileMenu->addAction(addCommand({QStringLiteral("file.exit"),
                                    tr("E&xit"),
                                    {},
                                    QKeySequence(QKeySequence::Quit),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [] { QApplication::closeAllWindows(); }}));

    auto* editMenu = new components::Menu(tr("Edit"), workbenchMenuBar_);
    editMenu->addAction(addCommand({QStringLiteral("edit.undo"), tr("Undo"), tr("Edit: Undo"),
                                    QKeySequence(QKeySequence::Undo),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [this] { editorArea_->undo(); }}));
    editMenu->addAction(addCommand({QStringLiteral("edit.redo"), tr("Redo"), tr("Edit: Redo"),
                                    QKeySequence(QKeySequence::Redo),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [this] { editorArea_->redo(); }}));
    editMenu->addSeparator();
    editMenu->addAction(
        addCommand({QStringLiteral("edit.find"), tr("Find…"), tr("Edit: Find"),
                    QKeySequence(QKeySequence::Find),
                    CommandSurface::Menu | CommandSurface::ShortcutEditor, [this] {
                        QWidget* focus = QApplication::focusWidget();
                        const bool terminalFocused =
                            focus && terminalPanel_ && terminalPanel_->isAncestorOf(focus);
                        const bool noDocument = editorSessionController_->currentFile().isEmpty();
                        if (terminalFocused ||
                            (noDocument && bottomTabs_->currentWidget() == terminalPanel_)) {
                            terminalPanel_->showFind();
                        } else {
                            editorArea_->showFindReplace(false);
                        }
                    }}));
    editMenu->addAction(addCommand({QStringLiteral("edit.replace"), tr("Replace…"),
                                    tr("Edit: Replace"), QKeySequence(QKeySequence::Replace),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [this] { editorArea_->showFindReplace(true); }}));

    auto* viewMenu = new components::Menu(tr("View"), workbenchMenuBar_);
    viewMenu->addAction(
        addCommand({QStringLiteral("view.explorer"), tr("Explorer"), tr("View: Explorer"),
                    primaryShortcut(Qt::Key_E, Qt::ShiftModifier),
                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                    [this] { workbenchController_->showSidebar(SidebarView::Explorer); }}));
    viewMenu->addAction(addCommand({QStringLiteral("view.search"), tr("Search"), tr("View: Search"),
                                    primaryShortcut(Qt::Key_F, Qt::ShiftModifier),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor, [this] {
                                        workspaceSearchRoot_ =
                                            workbenchController_->workspacePath();
                                        workbenchController_->showSidebar(SidebarView::Search);
                                        searchInput_->setFocus();
                                        searchInput_->selectAll();
                                    }}));
    viewMenu->addSeparator();
    viewMenu->addAction(addCommand({QStringLiteral("view.terminal"), tr("Terminal"),
                                    tr("View: Terminal"), QKeySequence(QStringLiteral("Ctrl+`")),
                                    CommandSurface::Menu | CommandSurface::ShortcutEditor,
                                    [this] { toggleTerminalPanel(); }}));
    auto* resetEditorZoom =
        addCommand({QStringLiteral("view.resetEditorZoom"), tr("Reset Editor Zoom"),
                    tr("View: Reset Editor Zoom"), primaryShortcut(Qt::Key_0),
                    CommandSurface::ShortcutEditor, [this] { editorArea_->resetEditorZoom(); }});
    resetEditorZoom->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(resetEditorZoom);
    auto* terminalMenu = new components::Menu(tr("Terminal"), workbenchMenuBar_);
    auto* newTerminal =
        addCommand({QStringLiteral("terminal.new"), tr("New Terminal"),
                    tr("Terminal: New Terminal"), QKeySequence(QStringLiteral("Ctrl+Shift+`")),
                    CommandSurface::Menu | CommandSurface::ShortcutEditor, [this] {
                        workbenchController_->showBottomPanel(BottomPanelView::Terminal);
                        terminalPanel_->newTerminal();
                    }});
    newTerminal->setObjectName(QStringLiteral("newTerminalAction"));
    terminalMenu->addAction(newTerminal);
    auto* helpMenu = new components::Menu(tr("Help"), workbenchMenuBar_);
    helpMenu->addAction(addCommand(
        {QStringLiteral("help.about"), tr("About LiteCode"), {}, {}, CommandSurface::Menu, [this] {
             ConfirmationDialog::showMessage(
                 this, tr("About LiteCode"),
                 tr("LiteCode 0.1.0\nA lightweight native desktop IDE."));
         }}));

    auto* settingsAction = addCommand({QStringLiteral("preferences.settings"), tr("Settings…"),
                                       tr("Preferences: Settings"), primaryShortcut(Qt::Key_Comma),
                                       CommandSurface::Palette | CommandSurface::ShortcutEditor,
                                       [this] { configureSettings(); }});
    auto* keyboardShortcutsAction =
        addCommand({QStringLiteral("preferences.keyboardShortcuts"), tr("Keyboard Shortcuts…"),
                    tr("Preferences: Keyboard Shortcuts"), primaryChord(Qt::Key_K, Qt::Key_S),
                    CommandSurface::Palette | CommandSurface::ShortcutEditor,
                    [this] { configureKeyboardShortcuts(); }});
    addAction(settingsAction);
    addAction(keyboardShortcutsAction);

    const QList<components::Menu*> topLevelMenus{fileMenu, editMenu, viewMenu, terminalMenu,
                                                 helpMenu};
    const int sharedMenuWidth = components::ComponentMetrics{}.menuWidth;
    int menuWidth = sharedMenuWidth;
    for (const auto* menu : topLevelMenus)
        menuWidth = qMax(menuWidth, menu->sizeHint().width());
    for (auto* menu : topLevelMenus)
        menu->setMinimumWidth(menuWidth);

    workbenchMenuBar_->clear();
    workbenchMenuBar_->addMenu(fileMenu);
    workbenchMenuBar_->addMenu(editMenu);
    workbenchMenuBar_->addMenu(viewMenu);
    workbenchMenuBar_->addMenu(terminalMenu);
    workbenchMenuBar_->addMenu(helpMenu);
    if (normalizedStoredShortcut)
        persistSettings(false);
}

void MainWindow::buildNavigationBar() {
    titleBar_ = new WorkbenchTitleBar(workbenchMenuBar_, this);
    connect(titleBar_, &WorkbenchTitleBar::togglePrimarySidebarRequested, this,
            [this] { workbenchController_->togglePrimarySidebar(); });
    connect(titleBar_, &WorkbenchTitleBar::toggleBottomPanelRequested, this,
            [this] { toggleTerminalPanel(); });
    setMenuWidget(titleBar_);
}

void MainWindow::buildActivityBar() {
    activityBar_ = new QToolBar(tr("Activity"), this);
    auto* bar = activityBar_;
    bar->setObjectName(QStringLiteral("activityBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(24, 24));
    bar->setFixedWidth(ThemeMetrics::activityBarWidth);
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto* group = new QActionGroup(bar);
    group->setExclusive(true);
    explorerAction_ =
        bar->addAction(workbenchActivityIcon(QStringLiteral(":/icons/files.svg"),
                                             QStringLiteral(":/icons/files-active.svg")),
                       tr("Explorer"));
    searchAction_ =
        bar->addAction(workbenchActivityIcon(QStringLiteral(":/icons/search.svg"),
                                             QStringLiteral(":/icons/search-active.svg")),
                       tr("Search"));
    explorerAction_->setCheckable(true);
    searchAction_->setCheckable(true);
    explorerAction_->setChecked(true);
    group->addAction(explorerAction_);
    group->addAction(searchAction_);
    auto* activitySpacer = new QWidget(bar);
    activitySpacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    bar->addWidget(activitySpacer);
    manageAction_ = bar->addAction(QIcon(), tr("Manage"));
    manageAction_->setObjectName(QStringLiteral("manageAction"));
    setThemedIcon(manageAction_, QStringLiteral(":/icons/settings.svg"));
    connect(manageAction_, &QAction::triggered, this, [this, bar] {
        components::Menu menu(this);
        menu.setObjectName(QStringLiteral("manageMenu"));
        menu.addAction(commands_->action(QStringLiteral("preferences.settings")));
        menu.addAction(commands_->action(QStringLiteral("preferences.keyboardShortcuts")));
        auto* theme = new components::Menu(tr("Themes"), &menu);
        theme->setObjectName(QStringLiteral("themeMenu"));
        menu.addMenu(theme);
        auto* themeGroup = new QActionGroup(theme);
        themeGroup->setExclusive(true);
        auto* darkTheme = theme->addAction(tr("Dark Theme"), this,
                                           [this] { applyTheme(QStringLiteral("dark")); });
        auto* lightTheme = theme->addAction(tr("Light Theme"), this,
                                            [this] { applyTheme(QStringLiteral("light")); });
        for (QAction* action : {darkTheme, lightTheme}) {
            action->setCheckable(true);
            themeGroup->addAction(action);
        }
        (settings_.theme() == QStringLiteral("light") ? lightTheme : darkTheme)->setChecked(true);
        QWidget* button = bar->widgetForAction(manageAction_);
        if (button == nullptr) {
            return;
        }
        menu.ensurePolished();
        menu.adjustSize();
        const QSize iconSize = bar->iconSize();
        const QPoint iconTopLeft((button->width() - iconSize.width()) / 2,
                                 (button->height() - iconSize.height()) / 2);
        const QRect iconBounds(button->mapToGlobal(iconTopLeft), iconSize);
        menu.exec(PopupPositioner::anchoredTopLeft(menu.size(), iconBounds, this,
                                                   PopupVerticalPlacement::AboveFirst,
                                                   PopupHorizontalPlacement::AfterAnchor));
    });
    addToolBar(Qt::LeftToolBarArea, bar);

    connect(explorerAction_, &QAction::triggered, this,
            [this] { workbenchController_->showSidebar(SidebarView::Explorer); });
    connect(searchAction_, &QAction::triggered, this, [this] {
        workspaceSearchRoot_ = workbenchController_->workspacePath();
        workbenchController_->showSidebar(SidebarView::Search);
        searchInput_->setFocus();
        searchInput_->selectAll();
    });
}

void MainWindow::buildWorkspaceDock() {
    workspaceDock_ = new WorkbenchDock(tr("Explorer"), true, this);
    workspaceDock_->setObjectName(QStringLiteral("workspaceDock"));
    workspaceDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    QWidget* workspaceHeader =
        createWorkbenchDockHeader(workspaceDock_, tr("Explorer"), &workspaceTitle_, false);
    workspaceDock_->setTitleBarWidget(workspaceHeader);
    searchHeaderActions_ = new QWidget(workspaceHeader);
    searchHeaderActions_->setObjectName(QStringLiteral("searchHeaderActions"));
    auto* searchHeaderLayout = new QHBoxLayout(searchHeaderActions_);
    searchHeaderLayout->setContentsMargins(0, 0, 0, 0);
    searchHeaderLayout->setSpacing(1);
    const auto makeSearchHeaderAction = [this](const QString& icon, const QString& tooltip) {
        auto* button = new components::IconButton(searchHeaderActions_);
        button->setObjectName(QStringLiteral("searchHeaderAction"));
        button->setAutoRaise(true);
        button->setIconSize(QSize(16, 16));
        button->setToolTip(tooltip);
        setThemedIcon(button, icon);
        searchHeaderActions_->layout()->addWidget(button);
        return button;
    };
    auto* refreshSearch =
        makeSearchHeaderAction(QStringLiteral(":/icons/refresh.svg"), tr("Refresh Search"));
    auto* clearSearch =
        makeSearchHeaderAction(QStringLiteral(":/icons/clear.svg"), tr("Clear Search Results"));
    static_cast<QHBoxLayout*>(workspaceHeader->layout())->addWidget(searchHeaderActions_);
    searchHeaderActions_->hide();
    workspacePages_ = new QStackedWidget(workspaceDock_);
    workspacePages_->setObjectName(QStringLiteral("workspacePages"));
    workspacePages_->setAttribute(Qt::WA_StyledBackground, true);

    fileSystemModel_ = new ExplorerFileSystemModel(workspacePages_);
    fileSystemModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    fileSystemModel_->setReadOnly(false);
    fileTree_ = new ExplorerTreeView(*fileSystemModel_, workspacePages_);
    TransientScrollBars::install(fileTree_);
    fileTree_->setObjectName(QStringLiteral("fileTree"));
    fileTree_->setAccessibleName(tr("Workspace files"));
    fileTree_->setIconSize(QSize(18, 18));
    fileTree_->setIndentation(16);
    fileTree_->setHeaderHidden(true);
    fileTree_->setAnimated(false);
    fileTree_->setMouseTracking(true);
    fileTree_->setUniformRowHeights(false);
    // QTreeView's native row selection starts after the branch column. LiteCode paints a single
    // full-width Explorer row instead, while the current index still drives actions/navigation.
    fileTree_->setSelectionMode(QAbstractItemView::NoSelection);
    fileTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int column = 1; column < fileSystemModel_->columnCount(); ++column) {
        fileTree_->hideColumn(column);
    }
    connect(fileTree_, &QTreeView::clicked, this, &MainWindow::openTreeEntry);
    connect(fileTree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                explorerController_->setSelectedPath(
                    current.isValid() ? fileSystemModel_->filePath(current) : QString{});
                if (current.isValid())
                    setExplorerRootSelected(false);
            });
    fileTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    fileTree_->installEventFilter(this);
    fileTree_->viewport()->installEventFilter(this);
    connect(fileTree_, &QTreeView::customContextMenuRequested, this, &MainWindow::showFileTreeMenu);

    connect(fileTree_, &ExplorerTreeView::inlineEditAccepted, this, [this](const QString& name) {
        if (!name.isEmpty())
            (void)explorerController_->commitEdit(name);
    });
    connect(fileTree_, &ExplorerTreeView::inlineEditCancelled, explorerController_,
            &ExplorerController::cancelEdit);

    auto* explorerPage = new QWidget(workspacePages_);
    explorerPage->setObjectName(QStringLiteral("explorerPage"));
    explorerPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* explorerLayout = new QVBoxLayout(explorerPage);
    explorerLayout->setContentsMargins(0, 0, 0, 0);
    explorerLayout->setSpacing(0);
    workspaceRootBar_ = new QWidget(explorerPage);
    workspaceRootBar_->setObjectName(QStringLiteral("workspaceRootBar"));
    workspaceRootBar_->setProperty("selected", false);
    workspaceRootBar_->setFixedHeight(23);
    workspaceRootBar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* rootLayout = new QHBoxLayout(workspaceRootBar_);
    rootLayout->setContentsMargins(0, 0, 4, 0);
    rootLayout->setSpacing(1);
    auto* workspaceRootChevron = new components::IconButton(workspaceRootBar_);
    workspaceRootChevron->setObjectName(QStringLiteral("workspaceRootChevron"));
    workspaceRootChevron->setFixedSize(20, 23);
    workspaceRootChevron->setIconSize(QSize(16, 16));
    setThemedIcon(workspaceRootChevron, QStringLiteral(":/icons/chevron-down.svg"));
    rootLayout->addWidget(workspaceRootChevron);
    workspaceRootLabel_ = new components::IconButton(workspaceRootBar_);
    workspaceRootLabel_->setObjectName(QStringLiteral("workspaceRootLabel"));
    workspaceRootLabel_->setText(tr("No Folder Opened"));
    workspaceRootLabel_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    workspaceRootLabel_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    workspaceRootLabel_->setCheckable(true);
    workspaceRootLabel_->setChecked(true);
    connect(workspaceRootChevron, &QToolButton::clicked, workspaceRootLabel_,
            [this] { workspaceRootLabel_->toggle(); });
    connect(workspaceRootChevron, &QToolButton::pressed, this,
            [this] { setExplorerRootSelected(true); });
    connect(workspaceRootLabel_, &QToolButton::pressed, this,
            [this] { setExplorerRootSelected(true); });
    connect(workspaceRootLabel_, &QToolButton::toggled, this,
            [this, workspaceRootChevron](bool expanded) {
                fileTree_->setVisible(expanded);
                setThemedIcon(workspaceRootChevron,
                              expanded ? QStringLiteral(":/icons/chevron-down.svg")
                                       : QStringLiteral(":/icons/chevron-right.svg"));
            });
    auto makeExplorerAction = [this, rootLayout](const QString& icon, const QString& tooltip) {
        auto* button = new components::IconButton(workspaceRootBar_);
        button->setObjectName(QStringLiteral("explorerActionButton"));
        setThemedIcon(button, icon);
        button->setIconSize(QSize(17, 17));
        button->setToolTip(tooltip);
        rootLayout->addWidget(button);
        return button;
    };
    rootLayout->addWidget(workspaceRootLabel_);
    rootLayout->addStretch(1);
    auto* newFile = makeExplorerAction(QStringLiteral(":/icons/new-file.svg"), tr("New File"));
    auto* newFolder =
        makeExplorerAction(QStringLiteral(":/icons/new-folder.svg"), tr("New Folder"));
    auto* refresh =
        makeExplorerAction(QStringLiteral(":/icons/refresh.svg"), tr("Refresh Explorer"));
    auto* collapse =
        makeExplorerAction(QStringLiteral(":/icons/collapse.svg"), tr("Collapse Folders"));
    const auto beginExplorerCreate = [this](bool directory) {
        if (!explorerController_->hasWorkspace()) {
            chooseWorkspaceFolder();
            return;
        }
        // Inline creation lives in the tree viewport. Ensure the workspace root is expanded
        // before creating it so header actions remain usable while the tree is collapsed.
        workspaceRootLabel_->setChecked(true);
        explorerController_->beginCreate(directory);
    };
    connect(newFile, &QToolButton::clicked, this,
            [beginExplorerCreate] { beginExplorerCreate(false); });
    connect(newFolder, &QToolButton::clicked, this,
            [beginExplorerCreate] { beginExplorerCreate(true); });
    connect(refresh, &QToolButton::clicked, explorerController_,
            &ExplorerController::requestRefresh);
    connect(collapse, &QToolButton::clicked, fileTree_, &QTreeView::collapseAll);
    connect(fileSystemModel_, &ExplorerFileSystemModel::directoryLoaded, this,
            [this](const QString&) { updateExplorerExclusions(); });
    connect(explorerController_, &ExplorerController::workspaceRootChanged, workbenchController_,
            &WorkbenchController::setWorkspacePath);
    connect(explorerController_, &ExplorerController::refreshRequested, this,
            [this](const QString& rootPath) {
                const QModelIndex root = fileSystemModel_->setRootPath(rootPath);
                fileTree_->setRootIndex(root);
                updateExplorerExclusions();
            });
    connect(explorerController_, &ExplorerController::editRequested, this,
            &MainWindow::showExplorerEdit);
    connect(explorerController_, &ExplorerController::editCancelled, this,
            [this] { hideExplorerCreateRow(); });
    connect(explorerController_, &ExplorerController::operationFailed, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 5000);
                if (explorerController_->activeEdit())
                    fileTree_->setInlineEditorError(message);
            });
    connect(explorerController_, &ExplorerController::entryCreated, this,
            [this](const QString& path, bool directory) {
                hideExplorerCreateRow();
                fileTree_->setFocus();
                QTimer::singleShot(100, this, [this, path, directory] {
                    const QModelIndex index = fileSystemModel_->index(path);
                    if (index.isValid()) {
                        fileTree_->setCurrentIndex(index);
                        fileTree_->scrollTo(index);
                        if (directory) {
                            fileTree_->expand(index);
                        }
                    }
                });
                if (!directory) {
                    QString error;
                    if (!editorSessionController_->openFile(path, &error)) {
                        statusBar()->showMessage(error, 5000);
                    }
                }
            });
    connect(explorerController_, &ExplorerController::entryRenamed, this,
            [this](const QString& previousPath, const QString& path) {
                editorSessionController_->handlePathRenamed(previousPath, path);
                hideExplorerCreateRow();
                fileTree_->setFocus();
                QTimer::singleShot(100, this, [this, path] {
                    const QModelIndex index = fileSystemModel_->index(path);
                    if (index.isValid()) {
                        fileTree_->setCurrentIndex(index);
                        fileTree_->scrollTo(index);
                    }
                });
            });
    connect(explorerController_, &ExplorerController::entryDeleted, editorSessionController_,
            &EditorSessionController::handlePathDeleted);
    connect(explorerController_, &ExplorerController::permanentDeleteConfirmationRequested, this,
            [this](const ExplorerDeleteRequest& request, const QString& reason) {
#ifdef Q_OS_WIN
                const QString trashName = tr("Recycle Bin");
#else
                const QString trashName = tr("Trash");
#endif
                const auto answer = ConfirmationDialog::ask(
                    this, tr("Delete Permanently"),
                    tr("Failed to delete using the %1. Do you want to permanently delete %2 "
                       "instead?\n\nThis cannot be undone.\n\n%3")
                        .arg(trashName, request.displayName, reason),
                    tr("Delete Permanently"), {}, tr("Cancel"));
                if (answer == ConfirmationChoice::Primary) {
                    explorerController_->confirmPermanentDelete(request.path);
                } else {
                    explorerController_->cancelDelete(request.path);
                }
            });
    auto* fileTreeHost = new QWidget(explorerPage);
    fileTreeHost->setObjectName(QStringLiteral("fileTreeHost"));
    fileTreeHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* fileTreeHostLayout = new QVBoxLayout(fileTreeHost);
    fileTreeHostLayout->setContentsMargins(0, 0, 0, 0);
    fileTreeHostLayout->setSpacing(0);
    fileTreeHostLayout->addWidget(fileTree_);
    explorerLayout->addWidget(workspaceRootBar_);
    explorerLayout->addWidget(fileTreeHost, 1);

    auto* searchPage = new QWidget(workspacePages_);
    searchPage->setObjectName(QStringLiteral("searchPage"));
    searchPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* searchLayout = new QVBoxLayout(searchPage);
    searchLayout->setContentsMargins(8, 6, 8, 0);
    searchLayout->setSpacing(4);

    auto* searchRow = new QWidget(searchPage);
    searchRow->setObjectName(QStringLiteral("workspaceSearchRow"));
    auto* searchRowLayout = new QHBoxLayout(searchRow);
    searchRowLayout->setContentsMargins(0, 0, 0, 0);
    searchRowLayout->setSpacing(4);
    replaceToggle_ = new components::IconButton(searchRow);
    replaceToggle_->setObjectName(QStringLiteral("searchReplaceToggle"));
    replaceToggle_->setAutoRaise(true);
    replaceToggle_->setCheckable(true);
    replaceToggle_->setIconSize(QSize(14, 14));
    replaceToggle_->setToolTip(tr("Toggle Replace"));
    setThemedIcon(replaceToggle_, QStringLiteral(":/icons/chevron-right.svg"));
    searchRowLayout->addWidget(replaceToggle_);

    auto* searchBox = new QWidget(searchRow);
    searchBox->setObjectName(QStringLiteral("workspaceSearchBox"));
    auto* searchBoxLayout = new QHBoxLayout(searchBox);
    searchBoxLayout->setContentsMargins(0, 0, 2, 0);
    searchBoxLayout->setSpacing(0);
    searchInput_ = new components::Input(searchBox);
    searchInput_->setObjectName(QStringLiteral("workspaceSearchInput"));
    searchInput_->setAccessibleName(tr("Search workspace contents"));
    searchInput_->setPlaceholderText(tr("Search (Up/Down for history)"));
    searchInput_->setFixedHeight(28);
    searchInput_->installEventFilter(this);
    searchBoxLayout->addWidget(searchInput_, 1);
    const auto makeSearchOption = [searchBox](const QString& text, const QString& tooltip) {
        auto* button = new components::IconButton(searchBox);
        button->setObjectName(QStringLiteral("workspaceSearchOption"));
        button->setText(text);
        button->setToolTip(tooltip);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFixedSize(25, 24);
        return button;
    };
    matchCaseButton_ = makeSearchOption(QStringLiteral("Aa"), tr("Match Case"));
    matchWholeWordButton_ = makeSearchOption(QStringLiteral("ab"), tr("Match Whole Word"));
    useRegexButton_ = makeSearchOption(QStringLiteral(".*"), tr("Use Regular Expression"));
    searchBoxLayout->addWidget(matchCaseButton_);
    searchBoxLayout->addWidget(matchWholeWordButton_);
    searchBoxLayout->addWidget(useRegexButton_);
    searchRowLayout->addWidget(searchBox, 1);

    replaceRow_ = new QWidget(searchPage);
    replaceRow_->setObjectName(QStringLiteral("workspaceReplaceRow"));
    auto* replaceRowLayout = new QHBoxLayout(replaceRow_);
    replaceRowLayout->setContentsMargins(22, 0, 0, 0);
    replaceRowLayout->setSpacing(4);
    auto* replaceBox = new QWidget(replaceRow_);
    replaceBox->setObjectName(QStringLiteral("workspaceSearchBox"));
    auto* replaceBoxLayout = new QHBoxLayout(replaceBox);
    replaceBoxLayout->setContentsMargins(0, 0, 2, 0);
    replaceBoxLayout->setSpacing(0);
    replaceInput_ = new components::Input(replaceBox);
    replaceInput_->setObjectName(QStringLiteral("workspaceReplaceInput"));
    replaceInput_->setAccessibleName(tr("Replace workspace matches"));
    replaceInput_->setPlaceholderText(tr("Replace"));
    replaceInput_->setFixedHeight(28);
    preserveCaseButton_ = makeSearchOption(QStringLiteral("AB"), tr("Preserve Case"));
    preserveCaseButton_->setParent(replaceBox);
    replaceBoxLayout->addWidget(replaceInput_, 1);
    replaceBoxLayout->addWidget(preserveCaseButton_);
    replaceRowLayout->addWidget(replaceBox, 1);
    replaceAllWorkspaceButton_ = new components::IconButton(replaceRow_);
    replaceAllWorkspaceButton_->setObjectName(QStringLiteral("workspaceReplaceAllButton"));
    replaceAllWorkspaceButton_->setToolTip(tr("Replace All"));
    replaceAllWorkspaceButton_->setIconSize(QSize(16, 16));
    setThemedIcon(replaceAllWorkspaceButton_, QStringLiteral(":/icons/replace-all.svg"));
    replaceAllWorkspaceButton_->setFixedHeight(28);
    replaceAllWorkspaceButton_->setEnabled(false);
    replaceRowLayout->addWidget(replaceAllWorkspaceButton_);
    replaceToggle_->setChecked(true);
    setThemedIcon(replaceToggle_, QStringLiteral(":/icons/chevron-down.svg"));

    searchResults_ = new components::List(searchPage);
    TransientScrollBars::install(searchResults_);
    searchResults_->setObjectName(QStringLiteral("workspaceSearchResults"));
    searchResults_->setMouseTracking(true);
    searchResults_->viewport()->setMouseTracking(true);
    searchResults_->setUniformItemSizes(false);
    searchResults_->setWordWrap(true);
    searchResults_->setResizeMode(QListView::Adjust);
    searchResults_->setItemDelegate(
        new SearchResultsDelegate(&currentSearchResults_, searchResults_));
    searchResults_->setFrameShape(QFrame::NoFrame);
    searchResults_->viewport()->setObjectName(QStringLiteral("workspaceSearchViewport"));
    searchResults_->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    searchResults_->setProperty("litecodeSearchReplaceVisible", true);
    searchResults_->viewport()->setProperty("litecodeSearchReplaceVisible", true);
    searchLayout->addWidget(searchRow);
    searchLayout->addWidget(replaceRow_);
    searchLayout->addWidget(searchResults_, 1);
    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(150);
    const auto searchInputsChanged = [this] {
        const bool hasQuery = !searchInput_->text().isEmpty();
        invalidateWorkspaceSearch(!hasQuery);
        if (hasQuery)
            searchTimer_->start();
        else
            searchTimer_->stop();
    };
    connect(searchInput_, &QLineEdit::textChanged, this, searchInputsChanged);
    connect(searchInput_, &QLineEdit::returnPressed, this, [this] {
        searchTimer_->stop();
        const QString query = searchInput_->text();
        if (!query.isEmpty()) {
            searchHistory_.removeAll(query);
            searchHistory_.prepend(query);
            if (searchHistory_.size() > 50) {
                searchHistory_.resize(50);
            }
            settings_.saveSearchHistory(workbenchController_->workspacePath(),
                                        QStringLiteral("workspaceSearch"), searchHistory_);
            searchHistoryIndex_ = -1;
        }
        runWorkspaceSearch();
    });
    connect(matchCaseButton_, &QToolButton::toggled, this, searchInputsChanged);
    connect(matchWholeWordButton_, &QToolButton::toggled, this, searchInputsChanged);
    connect(useRegexButton_, &QToolButton::toggled, this, searchInputsChanged);
    connect(replaceToggle_, &QToolButton::toggled, this, [this](bool expanded) {
        replaceRow_->setVisible(expanded);
        searchResults_->setProperty("litecodeSearchReplaceVisible", expanded);
        searchResults_->viewport()->setProperty("litecodeSearchReplaceVisible", expanded);
        searchResults_->viewport()->update();
        setThemedIcon(replaceToggle_, expanded ? QStringLiteral(":/icons/chevron-down.svg")
                                               : QStringLiteral(":/icons/chevron-right.svg"));
        if (expanded) {
            replaceInput_->setFocus();
        }
    });
    connect(replaceAllWorkspaceButton_, &QToolButton::clicked, this,
            [this] { replaceWorkspaceMatches(); });
    connect(refreshSearch, &QToolButton::clicked, this, &MainWindow::refreshWorkspaceSearch);
    connect(clearSearch, &QToolButton::clicked, this, [this] {
        searchInput_->clear();
        replaceInput_->clear();
        invalidateWorkspaceSearch(true);
        searchTimer_->stop();
        searchInput_->setFocus();
    });
    connect(searchTimer_, &QTimer::timeout, this, &MainWindow::runWorkspaceSearch);
    connect(workspaceSearch_, &workspace::WorkspaceSearch::resultBatchReady, this,
            [this](const workspace::SearchResultBatch& batch) {
                if (batch.operationId.value() == activeSearchOperationId_)
                    displaySearchResults(batch.results);
            });
    connect(workspaceSearch_, &workspace::WorkspaceSearch::operationCompleted, this,
            [this](const workspace::SearchOutcome& outcome) {
                if (outcome.operationId.value() != activeSearchOperationId_)
                    return;
                if (outcome.completion == core::CompletionKind::Cancelled)
                    return;
                if (outcome.completion != core::CompletionKind::Succeeded) {
                    searchResults_->clear();
                    currentSearchResults_.clear();
                    currentSearchResultPaths_.clear();
                    auto* item = new QListWidgetItem(
                        tr("Search failed: %1").arg(outcome.safeDiagnostic), searchResults_);
                    item->setFlags(Qt::NoItemFlags);
                    return;
                }
                searchResultsReady_ = true;
                searchResultsTruncated_ = outcome.truncated;
                if (!currentSearchResults_.isEmpty()) {
                    const QString matchCount = QString::number(outcome.totalMatches);
                    const QString fileCount = QString::number(outcome.totalFiles);
                    searchResults_->item(0)->setText(
                        outcome.countsComplete
                            ? tr("%1 result%2 in %3 file%4")
                                  .arg(matchCount,
                                       outcome.totalMatches == 1 ? QString{} : QStringLiteral("s"),
                                       fileCount,
                                       outcome.totalFiles == 1 ? QString{} : QStringLiteral("s"))
                            : tr("At least %1 results in %2 files").arg(matchCount, fileCount));
                }
                replaceAllWorkspaceButton_->setEnabled(!currentSearchResults_.isEmpty());
                replaceAllWorkspaceButton_->setToolTip(
                    outcome.truncated ? tr("Replace Listed Matches") : tr("Replace All"));
                if (outcome.truncated) {
                    auto* item = new QListWidgetItem(
                        outcome.safeDiagnostic == QStringLiteral("Search reached its result limit.")
                            ? tr("The result set only contains a subset of all matches. "
                                 "Be more specific in your search to narrow down the results.")
                            : tr("Search results are incomplete: %1").arg(outcome.safeDiagnostic));
                    item->setData(Qt::UserRole + 2, QStringLiteral("workspaceSearchWarning"));
                    item->setFlags(Qt::NoItemFlags);
                    searchResults_->insertItem(currentSearchResults_.isEmpty() ? 0 : 1, item);
                }
            });
    connect(workspaceReplace_, &workspace::WorkspaceReplace::completed, this,
            [this](const workspace::WorkspaceReplaceOutcome& outcome) {
                replaceAllWorkspaceButton_->setEnabled(searchResultsReady_ &&
                                                       !currentSearchResults_.isEmpty());
                if (outcome.cancelled)
                    return;
                if (!outcome.safeDiagnostic.isEmpty()) {
                    ConfirmationDialog::showMessage(
                        this,
                        outcome.changedFiles > 0 ? tr("Replace incomplete") : tr("Replace failed"),
                        outcome.safeDiagnostic);
                    runWorkspaceSearch();
                    return;
                }
                statusBar()->showMessage(tr("Replaced %1 occurrence(s) across %2 file(s).")
                                             .arg(outcome.replacements)
                                             .arg(outcome.changedFiles),
                                         5000);
                runWorkspaceSearch();
            });
    connect(searchResults_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        // Mouse clicks are handled by itemClicked; activation remains for Enter/keyboard use.
        if (QApplication::mouseButtons() != Qt::NoButton)
            return;
        if (item->data(Qt::UserRole + 2).toString() ==
            QStringLiteral("workspaceSearchFileHeader")) {
            toggleSearchResultFile(item);
            return;
        }
        if (item->data(Qt::UserRole).toString().isEmpty())
            return;
        QString error;
        if (!editorSessionController_->openMatch(item->data(Qt::UserRole).toString(),
                                                 item->data(Qt::UserRole + 1).toInt(),
                                                 item->data(Qt::UserRole + 3).toInt(),
                                                 item->data(Qt::UserRole + 4).toInt(), &error)) {
            ConfirmationDialog::showMessage(this, tr("Unable to open file"), error);
        }
    });
    connect(searchResults_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item->data(Qt::UserRole + 2).toString() !=
            QStringLiteral("workspaceSearchFileHeader")) {
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty()) {
                QString error;
                if (!editorSessionController_->openMatch(path, item->data(Qt::UserRole + 1).toInt(),
                                                         item->data(Qt::UserRole + 3).toInt(),
                                                         item->data(Qt::UserRole + 4).toInt(),
                                                         &error))
                    ConfirmationDialog::showMessage(this, tr("Unable to open file"), error);
            }
            return;
        }
        toggleSearchResultFile(item);
    });

    workspacePages_->addWidget(explorerPage);
    workspacePages_->addWidget(searchPage);
    auto* workspaceBody = new QWidget(workspaceDock_);
    workspaceBody->setObjectName(QStringLiteral("workspaceDockBody"));
    workspaceBody->setAttribute(Qt::WA_StyledBackground, true);
    auto* workspaceBodyLayout = new QHBoxLayout(workspaceBody);
    workspaceBodyLayout->setContentsMargins(0, 0, 0, 0);
    workspaceBodyLayout->setSpacing(0);
    workspaceBodyLayout->addWidget(workspacePages_, 1);
    workspaceDock_->setWidget(workspaceBody);
    addDockWidget(Qt::LeftDockWidgetArea, workspaceDock_);
    connect(workbenchController_, &WorkbenchController::workspaceChanged, this,
            [this](const QString& rootPath, const QString&) { applyWorkspace(rootPath); });
    connect(workbenchController_, &WorkbenchController::sidebarChanged, this,
            [this](SidebarView view, bool visible) {
                const int index = view == SidebarView::Explorer ? 0 : 1;
                workspacePages_->setCurrentIndex(index);
                workspaceTitle_->setText(view == SidebarView::Explorer ? tr("Explorer")
                                                                       : tr("Search"));
                searchHeaderActions_->setVisible(visible && view == SidebarView::Search);
                explorerAction_->setChecked(view == SidebarView::Explorer);
                searchAction_->setChecked(view == SidebarView::Search);
                workspaceDock_->setVisible(visible);
            });
    connect(workspaceDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        // A dock becomes temporarily non-visible when its top-level window is minimized or
        // covered. Only record a hidden sidebar when the dock itself was explicitly hidden.
        if (visible || workspaceDock_->isHidden())
            workbenchController_->setPrimarySidebarVisible(visible);
    });
}

void MainWindow::buildEditorShell() {
    editorArea_->setEditorFont(settings_.editorFontFamily(), settings_.editorFontSize(),
                               settings_.editorFontWeight());
    editorArea_->setIndentWidth(settings_.editorIndentWidth());
    editorArea_->setAutoGuessEncoding(settings_.fileAutoGuessEncoding());
    editorArea_->setDefaultEncoding(settings_.fileEncoding());
    editorArea_->setDarkTheme(settings_.theme() != QStringLiteral("light"));
    connect(editorArea_, &EditorArea::cursorPositionChanged, this, [this](int line, int column) {
        cursorStatus_->setText(tr("Ln %1, Col %2  ").arg(line).arg(column));
    });
    connect(editorArea_, &EditorArea::revealFileRequested, this,
            [this](const QString&) { revealActiveFile(); });
    connect(editorArea_, &EditorArea::revealInFileExplorerRequested, this,
            &MainWindow::revealInFileExplorer);
    connect(editorSessionController_, &EditorSessionController::currentFileChanged, this,
            [this](const QString& path) {
                const bool hasDocument = !path.isEmpty();
                updateEncodingStatus(editorSessionController_->currentEncoding());
                (void)commands_->setEnabled(QStringLiteral("file.save"), hasDocument);
                (void)commands_->setEnabled(QStringLiteral("file.saveAs"), hasDocument);
                (void)commands_->setEnabled(QStringLiteral("file.changeEncoding"), hasDocument);
                (void)commands_->setEnabled(QStringLiteral("edit.find"), true);
                (void)commands_->setEnabled(QStringLiteral("edit.replace"), hasDocument);
            });
    connect(editorSessionController_, &EditorSessionController::currentEncodingChanged, this,
            &MainWindow::updateEncodingStatus);
    const bool hasDocument = !editorSessionController_->currentFile().isEmpty();
    (void)commands_->setEnabled(QStringLiteral("file.save"), hasDocument);
    (void)commands_->setEnabled(QStringLiteral("file.saveAs"), hasDocument);
    (void)commands_->setEnabled(QStringLiteral("file.changeEncoding"), hasDocument);
    (void)commands_->setEnabled(QStringLiteral("edit.find"), true);
    (void)commands_->setEnabled(QStringLiteral("edit.replace"), hasDocument);
    editorSessionController_->setConfirmationHandler(
        [this](const EditorConfirmation& request) -> EditorDecision {
            if (request.kind == EditorConfirmationKind::ExternalModification) {
                const QString prompt =
                    request.hasUnsavedChanges
                        ? tr("%1 changed outside LiteCode and has unsaved edits. Reload and "
                             "discard them?")
                              .arg(request.displayName)
                        : tr("%1 changed outside LiteCode. Reload it?").arg(request.displayName);
                return ConfirmationDialog::ask(this, tr("File changed on disk"), prompt,
                                               tr("Reload"),
                                               tr("Keep")) == ConfirmationChoice::Primary
                           ? EditorDecision::Reload
                           : EditorDecision::Keep;
            }
            const auto answer = ConfirmationDialog::ask(
                this, tr("Unsaved changes"),
                tr("Save changes to %1 before closing?").arg(request.displayName), tr("Save"),
                tr("Discard"), tr("Cancel"));
            if (answer == ConfirmationChoice::Primary)
                return EditorDecision::Save;
            if (answer == ConfirmationChoice::Secondary)
                return EditorDecision::Discard;
            return EditorDecision::Cancel;
        });
    connect(editorSessionController_, &EditorSessionController::statusMessageRequested, this,
            [this](const QString& message, int timeoutMs) {
                statusBar()->showMessage(message, timeoutMs);
            });
    connect(editorSessionController_, &EditorSessionController::operationFailed, this,
            [this](const QString& title, const QString& message) {
                ConfirmationDialog::showMessage(this, title, message);
            });
    setCentralWidget(editorArea_);
}

void MainWindow::buildBottomPanel() {
    bottomDock_ = new WorkbenchDock(tr("Panel"), false, this);
    bottomDock_->setObjectName(QStringLiteral("bottomDock"));
    bottomDock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    auto* bottomTitleBar = new QWidget(bottomDock_);
    bottomTitleBar->setObjectName(QStringLiteral("hiddenDockHeader"));
    bottomTitleBar->setFixedHeight(0);
    bottomDock_->setTitleBarWidget(bottomTitleBar);

    auto* panelContainer = new QWidget(bottomDock_);
    panelContainer->setObjectName(QStringLiteral("bottomPanelContainer"));
    auto* panelLayout = new QVBoxLayout(panelContainer);
    panelLayout->setContentsMargins(1, 1, 1, 0);
    panelLayout->setSpacing(0);

    bottomTabs_ = new QTabWidget(panelContainer);
    bottomTabs_->setObjectName(QStringLiteral("bottomTabs"));
    bottomTabs_->setAccessibleName(tr("Bottom panel"));
    bottomTabs_->setDocumentMode(true);
    bottomTabs_->tabBar()->hide();
    terminalPanel_ = new TerminalPanel(bottomTabs_);
    terminalPanel_->setDefaultProfile(settings_.terminalProfile());
    terminalPanel_->setTerminalFont(
        QFont(settings_.terminalFontFamily(), settings_.terminalFontSize()));
    connect(terminalPanel_, &TerminalPanel::fileLinkActivated, this,
            [this](const QString& filePath, int line, int) {
                QString error;
                if (!editorSessionController_->openLocation(filePath, line, &error) &&
                    !error.isEmpty())
                    statusBar()->showMessage(error, 5000);
            });
    bottomTabs_->addTab(terminalPanel_, tr("Terminal"));
    connect(terminalPanel_, &TerminalPanel::closePanelRequested, workbenchController_,
            [this] { workbenchController_->setBottomPanelVisible(false); });
    connect(terminalPanel_, &TerminalPanel::toggleMaximizeRequested, this,
            [this] { setBottomPanelMaximized(!bottomPanelMaximized_); });

    auto* panelHeader = new QWidget(panelContainer);
    panelHeader->setObjectName(QStringLiteral("bottomPanelHeader"));
    panelHeader->setFixedHeight(35);
    auto* headerLayout = new QHBoxLayout(panelHeader);
    headerLayout->setContentsMargins(12, 0, 0, 0);
    headerLayout->setSpacing(0);
    auto* terminalTitleBlock = new QWidget(panelHeader);
    terminalTitleBlock->setObjectName(QStringLiteral("bottomPanelTitleBlock"));
    auto* terminalTitleLayout = new QVBoxLayout(terminalTitleBlock);
    terminalTitleLayout->setContentsMargins(0, 0, 0, 0);
    terminalTitleLayout->setSpacing(0);
    auto* terminalTitle = new QLabel(tr("Terminal"), terminalTitleBlock);
    terminalTitle->setObjectName(QStringLiteral("bottomPanelTitle"));
    terminalTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* terminalTitleUnderline = new QFrame(terminalTitleBlock);
    terminalTitleUnderline->setObjectName(QStringLiteral("bottomPanelTitleUnderline"));
    terminalTitleUnderline->setFixedHeight(1);
    terminalTitleLayout->addWidget(terminalTitle);
    terminalTitleLayout->addWidget(terminalTitleUnderline);
    headerLayout->addWidget(terminalTitleBlock);
    headerLayout->addStretch(1);
    terminalPanel_->controlsWidget()->setParent(panelHeader);
    headerLayout->addWidget(terminalPanel_->controlsWidget());

    const auto updatePanelHeader = [this](int index) {
        const QWidget* current = bottomTabs_->widget(index);
        terminalPanel_->controlsWidget()->setVisible(current == terminalPanel_);
        settings_.saveBottomPanelTab(index);
        if (!restoringSession_)
            persistSettings();
    };
    connect(bottomTabs_, &QTabWidget::currentChanged, this, updatePanelHeader);
    updatePanelHeader(bottomTabs_->currentIndex());

    panelLayout->addWidget(panelHeader);
    panelLayout->addWidget(bottomTabs_, 1);
    bottomDock_->setWidget(panelContainer);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock_);
    connect(workbenchController_, &WorkbenchController::bottomPanelChanged, this,
            [this](BottomPanelView view, bool visible) {
                Q_UNUSED(view);
                if (!visible && bottomPanelMaximized_)
                    setBottomPanelMaximized(false);
                bottomTabs_->setCurrentWidget(terminalPanel_);
                bottomDock_->setVisible(visible);
            });
    connect(bottomDock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        // Do not turn a temporary window-state transition into a persistent panel close.
        if (visible || bottomDock_->isHidden())
            workbenchController_->setBottomPanelVisible(visible);
    });
    workbenchController_->showBottomPanel(BottomPanelView::Terminal);
}

void MainWindow::buildStatusBar() {
    statusBar()->setObjectName(QStringLiteral("statusBar"));
    cursorStatus_ = new QLabel(tr("Ln 1, Col 1  "), statusBar());
    encodingStatusButton_ = new components::IconButton(statusBar());
    encodingStatusButton_->setObjectName(QStringLiteral("encodingStatusButton"));
    encodingStatusButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    encodingStatusButton_->setToolTip(tr("Select File Encoding"));
    connect(encodingStatusButton_, &QToolButton::clicked, this, &MainWindow::showEncodingMenu);
    indentStatusButton_ = new components::IconButton(statusBar());
    indentStatusButton_->setObjectName(QStringLiteral("indentStatusButton"));
    indentStatusButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    indentStatusButton_->setToolTip(tr("Choose the number of spaces inserted by Tab"));
    auto* indentMenu = new components::Menu(indentStatusButton_);
    for (const int width : {2, 4, 8}) {
        QAction* action = indentMenu->addAction(tr("Spaces: %1").arg(width));
        connect(action, &QAction::triggered, this, [this, width] { setEditorIndentWidth(width); });
    }
    connect(indentStatusButton_, &QToolButton::clicked, this, [this, indentMenu] {
        PopupPositioner::popupMenu(*indentMenu, indentStatusButton_, this,
                                   PopupVerticalPlacement::AboveFirst,
                                   PopupHorizontalPlacement::AlignRight);
    });
    setEditorIndentWidth(settings_.editorIndentWidth());
    statusBar()->addPermanentWidget(cursorStatus_);
    statusBar()->addPermanentWidget(encodingStatusButton_);
    statusBar()->addPermanentWidget(indentStatusButton_);
    updateEncodingStatus(core::TextEncoding::Utf8);
}

void MainWindow::toggleTerminalPanel() {
    if (workbenchController_->bottomPanelVisible()) {
        workbenchController_->setBottomPanelVisible(false);
        return;
    }
    workbenchController_->showBottomPanel(BottomPanelView::Terminal);
    if (!terminalPanel_->hasTerminals())
        terminalPanel_->newTerminal();
}

void MainWindow::setBottomPanelMaximized(bool maximized) {
    if (bottomPanelMaximized_ == maximized || !bottomDock_ || !editorArea_)
        return;
    if (maximized) {
        bottomPanelRestoreHeight_ = bottomDock_->height();
        bottomPanelMaximized_ = true;
        editorArea_->hide();
        resizeDocks({bottomDock_}, {height()}, Qt::Vertical);
        return;
    }
    bottomPanelMaximized_ = false;
    editorArea_->show();
    const int restoreHeight =
        bottomPanelRestoreHeight_ > 0 ? bottomPanelRestoreHeight_ : ThemeMetrics::bottomPanelHeight;
    resizeDocks({bottomDock_}, {restoreHeight}, Qt::Vertical);
}

void MainWindow::showEncodingMenu() {
    const QString filePath = editorSessionController_->currentFile();
    if (filePath.isEmpty())
        return;

    QString selectedAction;
    {
        QuickOpenDialog actionPicker(
            tr("Change File Encoding"), tr("Select Action"),
            QVector<QuickPickItem>{{QStringLiteral("reopen"), tr("Reopen with Encoding"), {}},
                                   {QStringLiteral("save"), tr("Save with Encoding"), {}}},
            QStringLiteral("reopen"), this);
        if (actionPicker.exec() != QDialog::Accepted)
            return;
        selectedAction = actionPicker.selectedItemId();
    }

    QTimer::singleShot(0, this, [this, filePath, selectedAction] {
        if (editorSessionController_->currentFile() != filePath)
            return;
        const bool reopen = selectedAction == QStringLiteral("reopen");
        const QVector<core::TextEncoding> encodings =
            reopen ? core::supportedTextDecodingEncodings() : core::supportedTextEncodings();
        const QString placeholder = reopen ? tr("Select File Encoding to Reopen File")
                                           : tr("Select File Encoding to Save File");
        const std::optional<core::TextEncoding> selected = chooseTextEncoding(
            filePath, placeholder, encodings, editorSessionController_->currentEncoding(), reopen);
        if (!selected)
            return;
        if (reopen)
            reopenCurrentWithEncoding(*selected);
        else
            saveCurrentWithEncoding(*selected);
    });
}

std::optional<core::TextEncoding>
MainWindow::chooseTextEncoding(const QString& filePath, const QString& placeholder,
                               const QVector<core::TextEncoding>& encodings,
                               std::optional<core::TextEncoding> currentEncoding,
                               bool suggestFromContent) {
    std::optional<core::TextEncoding> guessed;
    if (suggestFromContent) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray sample = file.read(64 * 1024);
            guessed = core::textEncodingFromByteOrderMark(sample);
            if (!guessed)
                guessed = core::guessTextEncoding(sample);
        }
    }
    if (guessed == core::TextEncoding::Utf8Bom && !encodings.contains(core::TextEncoding::Utf8Bom))
        guessed = core::TextEncoding::Utf8;
    if (currentEncoding == core::TextEncoding::Utf8Bom &&
        !encodings.contains(core::TextEncoding::Utf8Bom))
        currentEncoding = core::TextEncoding::Utf8;

    QVector<QuickPickItem> items;
    items.reserve(encodings.size());
    const auto appendEncoding = [&items](core::TextEncoding encoding, bool guessedFromContent) {
        QString description = core::textEncodingKey(encoding);
        if (guessedFromContent)
            description += QStringLiteral("    ") + tr("Guessed from content");
        items.append({core::textEncodingKey(encoding), core::textEncodingLongDisplayName(encoding),
                      description});
    };
    if (guessed && encodings.contains(*guessed))
        appendEncoding(*guessed, true);
    for (const core::TextEncoding encoding : encodings) {
        if (encoding != guessed)
            appendEncoding(encoding, false);
    }

    const QString initialId = currentEncoding && encodings.contains(*currentEncoding)
                                  ? core::textEncodingKey(*currentEncoding)
                              : guessed ? core::textEncodingKey(*guessed)
                                        : QString{};
    QuickOpenDialog picker(tr("File Encoding"), placeholder, std::move(items), initialId, this);
    if (picker.exec() != QDialog::Accepted)
        return std::nullopt;
    return core::textEncodingFromKey(picker.selectedItemId());
}

void MainWindow::reopenCurrentWithEncoding(core::TextEncoding encoding) {
    QString error;
    if (!editorSessionController_->reopenCurrentWithEncoding(encoding, &error)) {
        ConfirmationDialog::showMessage(this, tr("Reopen with Encoding failed"), error);
        return;
    }
    statusBar()->showMessage(tr("Reopened with %1").arg(core::textEncodingDisplayName(encoding)),
                             3000);
}

void MainWindow::saveCurrentWithEncoding(core::TextEncoding encoding) {
    QString error;
    if (!editorSessionController_->saveCurrentWithEncoding(encoding, &error)) {
        ConfirmationDialog::showMessage(this, tr("Save with Encoding failed"), error);
        return;
    }
    statusBar()->showMessage(tr("Saved with %1").arg(core::textEncodingDisplayName(encoding)),
                             3000);
}

void MainWindow::updateEncodingStatus(core::TextEncoding encoding) {
    if (!encodingStatusButton_)
        return;
    const bool hasDocument = !editorSessionController_->currentFile().isEmpty();
    encodingStatusButton_->setVisible(hasDocument);
    encodingStatusButton_->setText(hasDocument ? core::textEncodingDisplayName(encoding)
                                               : QString{});
}

void MainWindow::configureSettings() {
    if (settingsDialog_) {
        settingsDialog_->showNormal();
        settingsDialog_->raise();
        settingsDialog_->activateWindow();
        return;
    }

    QFont currentFont(settings_.editorFontFamily());
    currentFont.setPixelSize(settings_.editorFontSize());
    currentFont.setWeight(static_cast<QFont::Weight>(settings_.editorFontWeight()));
    auto* dialog = new SettingsDialog(currentFont, settings_.fileAutoGuessEncoding(),
                                      settings_.fileEncoding(), this);
    dialog->setUserEncodingError(!settings_.userEncodingValid());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    settingsDialog_ = dialog;
    settingsDialogChanged_ = false;
    const auto applySettingChange = [this, dialog] {
        const QSet<QString> changed = dialog->changedSettings();
        const QSet<QString> resets = dialog->resetSettings();
        if (changed.isEmpty() && resets.isEmpty())
            return;
        settingsDialogChanged_ = true;
        for (const QString& id : resets)
            settings_.resetSetting(id);
        const QFont editorFont = dialog->editorFont();
        if (changed.contains(QStringLiteral("editor.fontFamily")))
            settings_.saveEditorFontFamily(editorFont.family());
        if (changed.contains(QStringLiteral("editor.fontWeight")))
            settings_.saveEditorFontWeight(editorFont.weight());
        if (changed.contains(QStringLiteral("editor.fontSize")))
            settings_.saveEditorFontSize(editorFont.pixelSize());
        if (changed.contains(QStringLiteral("files.autoGuessEncoding")))
            settings_.saveFileAutoGuessEncoding(dialog->autoGuessEncoding());
        if (changed.contains(QStringLiteral("files.encoding")))
            settings_.saveFileEncoding(dialog->fileEncoding());
        persistSettings();
        applyEditorSettings();
        userSettingsSnapshot_ = captureUserSettings();
        QFont appliedFont(settings_.editorFontFamily());
        appliedFont.setPixelSize(settings_.editorFontSize());
        appliedFont.setWeight(static_cast<QFont::Weight>(settings_.editorFontWeight()));
        dialog->setValues(appliedFont, settings_.fileAutoGuessEncoding(), settings_.fileEncoding());
        dialog->setUserEncodingError(!settings_.userEncodingValid());
    };
    connect(dialog, &SettingsDialog::settingChanged, this,
            [applySettingChange](const QString&) { applySettingChange(); });
    connect(dialog, &SettingsDialog::resetRequested, this,
            [applySettingChange](const QString&) { applySettingChange(); });
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (settingsDialog_ == dialog)
            settingsDialog_ = nullptr;
        if (settingsDialogChanged_ && !closing_)
            runWorkspaceSearch();
        settingsDialogChanged_ = false;
    });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (settingsDialog_ == dialog)
            settingsDialog_ = nullptr;
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::applyEditorSettings() {
    editorArea_->setEditorFont(settings_.editorFontFamily(), settings_.editorFontSize(),
                               settings_.editorFontWeight());
    editorArea_->setAutoGuessEncoding(settings_.fileAutoGuessEncoding());
    editorArea_->setDefaultEncoding(settings_.fileEncoding());
}

void MainWindow::refreshSettingsDialogValues() {
    if (!settingsDialog_ || settingsDialog_->hasPendingChanges())
        return;
    settingsDialog_->setUserEncodingError(!settings_.userEncodingValid());
    QFont font(settings_.editorFontFamily());
    font.setPixelSize(settings_.editorFontSize());
    font.setWeight(static_cast<QFont::Weight>(settings_.editorFontWeight()));
    settingsDialog_->setValues(font, settings_.fileAutoGuessEncoding(), settings_.fileEncoding());
}

MainWindow::UserSettingsSnapshot MainWindow::captureUserSettings() const {
    return {settings_.editorFontFamily(),
            settings_.editorFontSize(),
            settings_.editorFontWeight(),
            settings_.fileAutoGuessEncoding(),
            settings_.fileEncoding(),
            settings_.editorIndentWidth(),
            settings_.terminalFontFamily(),
            settings_.terminalFontSize(),
            settings_.terminalProfile(),
            settings_.theme(),
            settings_.commandShortcutOverrides()};
}

void MainWindow::checkExternalUserSettings() {
    const core::TextEncoding previousEncoding = settings_.fileEncoding();
    if (!settings_.sync())
        return;
    const UserSettingsSnapshot current = captureUserSettings();
    if (current != userSettingsSnapshot_) {
        const UserSettingsSnapshot previous = userSettingsSnapshot_;
        userSettingsSnapshot_ = current;
        if (current.fontFamily != previous.fontFamily || current.fontSize != previous.fontSize ||
            current.fontWeight != previous.fontWeight || current.autoGuess != previous.autoGuess ||
            current.encoding != previous.encoding) {
            applyEditorSettings();
            refreshSettingsDialogValues();
        }
        if (current.indentWidth != previous.indentWidth) {
            editorArea_->setIndentWidth(current.indentWidth);
            indentStatusButton_->setText(tr("Spaces: %1").arg(current.indentWidth));
        }
        if (current.terminalFontFamily != previous.terminalFontFamily ||
            current.terminalFontSize != previous.terminalFontSize)
            terminalPanel_->setTerminalFont(
                QFont(current.terminalFontFamily, current.terminalFontSize));
        if (current.terminalProfile != previous.terminalProfile)
            terminalPanel_->setDefaultProfile(current.terminalProfile);
        if (current.theme != previous.theme)
            applyThemeVisuals(current.theme);
        if (current.shortcuts != previous.shortcuts) {
            commands_->reloadShortcuts(current.shortcuts);
            if (keyboardShortcutsDialog_)
                keyboardShortcutsDialog_->reloadFromRegistry();
        }
        if (!restoringSession_ && previousEncoding != settings_.fileEncoding())
            runWorkspaceSearch();
    }
    const bool invalidEncoding = !settings_.userEncodingValid();
    if (settingsDialog_)
        settingsDialog_->setUserEncodingError(invalidEncoding);
    if (invalidEncoding && !userEncodingErrorShown_) {
        userEncodingErrorShown_ = true;
        statusBar()->showMessage(
            tr("Settings contain an unsupported Files: Encoding value. UTF-8 is used until "
               "corrected."),
            10000);
    } else if (!invalidEncoding) {
        userEncodingErrorShown_ = false;
    }
}

bool MainWindow::persistSettings(bool showWarning) {
    if (settings_.sync()) {
        settingsWriteWarningShown_ = false;
        return true;
    }
    qWarning() << "LiteCode settings could not be saved";
    if (showWarning && !settingsWriteWarningShown_ && !closing_) {
        settingsWriteWarningShown_ = true;
        ConfirmationDialog::showMessage(
            this, tr("Settings could not be saved"),
            tr("Your changes are active for now, but may be lost after restarting LiteCode. "
               "Check that the selected settings location is writable."));
    }
    return false;
}

void MainWindow::setEditorIndentWidth(int width) {
    const int boundedWidth = qBound(1, width, 16);
    editorArea_->setIndentWidth(boundedWidth);
    settings_.saveEditorIndentWidth(boundedWidth);
    userSettingsSnapshot_.indentWidth = boundedWidth;
    if (!restoringSession_)
        persistSettings();
    if (indentStatusButton_ == nullptr)
        return;
    indentStatusButton_->setText(tr("Spaces: %1").arg(boundedWidth));
}

void MainWindow::restoreLayout() {
    resize(1440, 900);
    const auto layout = settings_.windowLayout();
    if (!layout.geometry.isEmpty()) {
        restoreGeometry(layout.geometry);
    }
    const bool restored = layout.version == workbenchLayoutVersion && !layout.state.isEmpty() &&
                          restoreState(layout.state, workbenchLayoutVersion);
    if (!restored) {
        resizeDocks({workspaceDock_}, {ThemeMetrics::primarySidebarWidth}, Qt::Horizontal);
        resizeDocks({bottomDock_}, {ThemeMetrics::bottomPanelHeight}, Qt::Vertical);
    }
    applySavedWorkbenchLayout();
}

void MainWindow::applySavedWorkbenchLayout() {
    const core::WorkbenchLayout layout = settings_.workbenchLayout();
    const auto sidebarView = static_cast<SidebarView>(
        qBound(static_cast<int>(SidebarView::Explorer), layout.primarySidebarView,
               static_cast<int>(SidebarView::Search)));

    // Keep the controller and the actual dock widgets in agreement. Calling showSidebar() alone
    // is insufficient when its logical state already says "visible" but restoreState() hid the
    // dock widget.
    workbenchController_->setPrimarySidebarVisible(false);
    workbenchController_->showSidebar(sidebarView);
    if (!layout.primarySidebarVisible)
        workbenchController_->setPrimarySidebarVisible(false);

    workbenchController_->setBottomPanelVisible(false);
    workbenchController_->showBottomPanel(BottomPanelView::Terminal);
    if (!layout.bottomPanelVisible)
        workbenchController_->setBottomPanelVisible(false);

    const int sidebarWidth = layout.primarySidebarWidth > 0 ? layout.primarySidebarWidth
                                                            : ThemeMetrics::primarySidebarWidth;
    const int bottomHeight =
        layout.bottomPanelHeight > 0 ? layout.bottomPanelHeight : ThemeMetrics::bottomPanelHeight;
    resizeDocks({workspaceDock_}, {sidebarWidth}, Qt::Horizontal);
    resizeDocks({bottomDock_}, {bottomHeight}, Qt::Vertical);
}

void MainWindow::restoreSession() {
    const QString savedWorkspace = settings_.workspaceFolder();
    if (!savedWorkspace.isEmpty()) {
        QString error;
        if (!explorerController_->openWorkspace(savedWorkspace, &error)) {
            qWarning().noquote() << QStringLiteral("session.workspace_restore_failed folder=%1")
                                        .arg(QFileInfo(savedWorkspace).fileName());
        }
    }

    QStringList files = settings_.openFiles();
    const QString activeFile = settings_.activeFile();
    // Opening a file selects its tab. Restore the active document last so the visible editor is
    // the same one the user left, while preserving all other open documents.
    if (!activeFile.isEmpty()) {
        files.removeAll(activeFile);
        files.append(activeFile);
    }
    for (const QString& filePath : files) {
        QString error;
        if (!editorSessionController_->openFile(filePath, &error)) {
            qWarning().noquote() << QStringLiteral("session.file_restore_failed path=%1")
                                        .arg(QFileInfo(filePath).fileName());
        }
    }
}

void MainWindow::enableSessionPersistence() {
    const auto persist = [this] {
        if (!restoringSession_ && !applyingSavedLayout_)
            saveLayout();
    };
    connect(workbenchController_, &WorkbenchController::workspaceChanged, this, persist);
    connect(editorSessionController_, &EditorSessionController::openFilesChanged, this, persist);
    connect(editorSessionController_, &EditorSessionController::currentFileChanged, this, persist);
    connect(workspaceDock_, &QDockWidget::visibilityChanged, this, persist);
    connect(bottomDock_, &QDockWidget::visibilityChanged, this, persist);
    connect(workspaceDock_, &QDockWidget::dockLocationChanged, this, persist);
    connect(bottomDock_, &QDockWidget::dockLocationChanged, this, persist);
}

void MainWindow::saveLayout() {
    settings_.saveWindowLayout(saveGeometry(), saveState(workbenchLayoutVersion),
                               workbenchLayoutVersion);
    settings_.saveSession(workbenchController_->workspacePath(),
                          editorSessionController_->openFiles(),
                          editorSessionController_->currentFile());
    settings_.saveWorkbenchLayout({
        !workspaceDock_->isHidden(),
        qMax(0, workspaceDock_->width()),
        static_cast<int>(workbenchController_->sidebarView()),
        !bottomDock_->isHidden(),
        qMax(0, bottomDock_->height()),
    });
    persistSettings(false);
}

void MainWindow::chooseWorkspaceFolder() {
    const QString initialPath = workbenchController_->hasWorkspace()
                                    ? workbenchController_->workspacePath()
                                    : QDir::homePath();
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Open Workspace Folder"), initialPath);
    if (folder.isEmpty()) {
        return;
    }
    if (QDir::cleanPath(folder) != QDir::cleanPath(workbenchController_->workspacePath()) &&
        !editorSessionController_->closeAllForWorkspaceChange())
        return;
    QString error;
    if (!explorerController_->openWorkspace(folder, &error)) {
        ConfirmationDialog::showMessage(this, tr("Unable to open workspace"), error);
        return;
    }
}

void MainWindow::applyWorkspace(const QString& rootPath) {
    workspaceReplace_->cancel();
    invalidateWorkspaceSearch(true);
    const QModelIndex rootIndex = fileSystemModel_->setRootPath(rootPath);
    fileTree_->setRootIndex(rootIndex);
    updateExplorerExclusions();
    workspaceTitle_->setText(tr("Explorer"));
    workspaceRootLabel_->setText(QFileInfo(rootPath).fileName());
    editorSessionController_->setWorkspacePath(rootPath);
    setWindowTitle(QStringLiteral("%1 — LiteCode").arg(QFileInfo(rootPath).fileName()));
    terminalPanel_->setWorkingDirectory(rootPath);
    workspaceSearchRoot_ = rootPath;
    searchHistory_ = settings_.searchHistory(rootPath, QStringLiteral("workspaceSearch"));
    searchHistoryIndex_ = -1;
    if (!searchInput_->text().isEmpty())
        searchTimer_->start();
    settings_.rememberWorkspaceFolder(rootPath);
}

void MainWindow::revealActiveFile() {
    const QString path = editorSessionController_->currentFile();
    if (path.isEmpty())
        return;
    const QModelIndex index = fileSystemModel_->index(path);
    if (!index.isValid()) {
        statusBar()->showMessage(tr("The active file is outside this workspace."), 4000);
        return;
    }

    workbenchController_->showSidebar(SidebarView::Explorer);
    QVector<QModelIndex> parents;
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
        parents.prepend(parent);
    for (const QModelIndex& parent : parents)
        fileTree_->expand(parent);
    fileTree_->setCurrentIndex(index);
    fileTree_->scrollTo(index, QAbstractItemView::PositionAtCenter);
    explorerController_->setSelectedPath(path);
    fileTree_->setFocus();
}

void MainWindow::revealInFileExplorer(const QString& path) {
    const QFileInfo selected(path);
    if (!selected.exists()) {
        statusBar()->showMessage(tr("The selected file no longer exists."), 4000);
        return;
    }
#ifdef Q_OS_WIN
    if (selected.isFile()) {
        QProcess::startDetached(
            QStringLiteral("explorer.exe"),
            {QStringLiteral("/select,"), QDir::toNativeSeparators(selected.absoluteFilePath())});
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(selected.absoluteFilePath()));
    }
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(selected.isFile() ? selected.dir().absolutePath()
                                                                    : selected.absoluteFilePath()));
#endif
}

void MainWindow::copyActiveFilePath() {
    const QString path = editorSessionController_->currentFile();
    if (path.isEmpty())
        return;
    QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    statusBar()->showMessage(tr("Copied active file path."), 2500);
}

void MainWindow::openTreeEntry(const QModelIndex& index) {
    const QFileInfo info = fileSystemModel_->fileInfo(index);
    if (!info.isFile()) {
        return;
    }
    QString error;
    if (!editorSessionController_->openFile(info.absoluteFilePath(), &error)) {
        ConfirmationDialog::showMessage(this, tr("Unable to open file"), error);
    }
}

void MainWindow::showFileTreeMenu(const QPoint& position) {
    if (!workbenchController_->hasWorkspace()) {
        return;
    }
    const QModelIndex index = fileTree_->indexAt(position);
    const QFileInfo selected = index.isValid() ? fileSystemModel_->fileInfo(index)
                                               : QFileInfo(workbenchController_->workspacePath());
    const QString parent =
        selected.isDir() ? selected.absoluteFilePath() : selected.dir().absolutePath();
    if (index.isValid()) {
        fileTree_->setCurrentIndex(index);
        explorerController_->setSelectedPath(selected.absoluteFilePath());
    }
    components::Menu menu(this);
    menu.setMinimumWidth(components::ComponentMetrics{}.contextMenuMinimumWidth);
    QAction* newFile = menu.addAction(tr("New File…"));
    QAction* newFolder = menu.addAction(tr("New Folder…"));
    menu.addSeparator();
    QAction* reveal = menu.addAction(tr("Reveal in File Explorer"));
    reveal->setShortcut(QKeySequence(QStringLiteral("Shift+Alt+R")));
    QAction* openTerminal = menu.addAction(tr("Open in Integrated Terminal"));
    QAction* findInFolder = selected.isDir() ? menu.addAction(tr("Find in Folder…")) : nullptr;
    if (findInFolder) {
        findInFolder->setShortcut(QKeySequence(QStringLiteral("Shift+Alt+F")));
    }
    menu.addSeparator();
    QAction* copyPath = menu.addAction(tr("Copy Path"));
    copyPath->setShortcut(QKeySequence(QStringLiteral("Shift+Alt+C")));
    QAction* copyRelativePath = menu.addAction(tr("Copy Relative Path"));
    menu.addSeparator();
    QAction* rename = index.isValid() ? menu.addAction(tr("Rename…")) : nullptr;
    QAction* remove = index.isValid() ? menu.addAction(tr("Delete…")) : nullptr;
    if (remove) {
        remove->setShortcut(QKeySequence::Delete);
    }
    QAction* chosen =
        PopupPositioner::execMenuAt(menu, fileTree_->viewport()->mapToGlobal(position), this);
    if (!chosen) {
        return;
    }
    QString error;
    if (chosen == newFile || chosen == newFolder) {
        explorerController_->beginCreate(chosen == newFolder, parent);
    } else if (chosen == reveal) {
        revealInFileExplorer(selected.absoluteFilePath());
    } else if (chosen == openTerminal) {
        terminalPanel_->openInDirectory(parent);
        workbenchController_->showBottomPanel(BottomPanelView::Terminal);
        terminalPanel_->setFocus();
    } else if (chosen == findInFolder) {
        workspaceSearchRoot_ = selected.absoluteFilePath();
        workbenchController_->showSidebar(SidebarView::Search);
        searchInput_->clear();
        searchInput_->setPlaceholderText(tr("Search in %1").arg(selected.fileName()));
        searchInput_->setFocus();
    } else if (chosen == copyPath) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(selected.absoluteFilePath()));
        statusBar()->showMessage(tr("Path copied"), 2500);
    } else if (chosen == copyRelativePath) {
        QApplication::clipboard()->setText(
            QDir::toNativeSeparators(QDir(workbenchController_->workspacePath())
                                         .relativeFilePath(selected.absoluteFilePath())));
        statusBar()->showMessage(tr("Relative path copied"), 2500);
    } else if (chosen == rename) {
        explorerController_->beginRename(selected.absoluteFilePath());
    } else if (chosen == remove) {
        requestExplorerDelete();
    }
}

void MainWindow::setExplorerRootSelected(bool selected) {
    if (workspaceRootBar_ == nullptr)
        return;
    if (selected && explorerController_ != nullptr && explorerController_->hasWorkspace()) {
        fileTree_->clearSelection();
        fileTree_->setCurrentIndex({});
        explorerController_->setSelectedPath(explorerController_->workspaceRoot());
    }
    if (workspaceRootBar_->property("selected").toBool() == selected)
        return;
    workspaceRootBar_->setProperty("selected", selected);
    workspaceRootBar_->style()->unpolish(workspaceRootBar_);
    workspaceRootBar_->style()->polish(workspaceRootBar_);
    workspaceRootBar_->update();
}

void MainWindow::requestExplorerDelete() {
    const QString path = explorerController_->selectedPath();
    const QVector<EditorDocumentInfo> affected =
        editorSessionController_->documentsAffectedByPath(path);
    const int dirtyCount = static_cast<int>(
        std::count_if(affected.cbegin(), affected.cend(),
                      [](const EditorDocumentInfo& document) { return document.modified; }));
    if (dirtyCount > 0) {
#ifdef Q_OS_WIN
        const QString primaryText = tr("Move to Recycle Bin");
#else
        const QString primaryText = tr("Move to Trash");
#endif
        const QFileInfo selected(path);
        QString message;
        if (selected.isDir()) {
            message = dirtyCount == 1
                          ? tr("You are deleting the folder %1 with unsaved changes in 1 file.")
                                .arg(selected.fileName())
                          : tr("You are deleting the folder %1 with unsaved changes in %2 files.")
                                .arg(selected.fileName())
                                .arg(dirtyCount);
        } else {
            message = tr("You are deleting %1 with unsaved changes.").arg(selected.fileName());
        }
        message += tr("\n\nYour unsaved changes will be lost if you continue.");
        if (ConfirmationDialog::ask(this, tr("Delete"), message, primaryText, {}, tr("Cancel")) !=
            ConfirmationChoice::Primary) {
            return;
        }
    }
    explorerController_->requestDelete();
}

void MainWindow::showExplorerEdit(const ExplorerEditRequest& request) {
    if (request.kind == ExplorerEditKind::Rename) {
        const QModelIndex index = fileSystemModel_->index(request.sourcePath);
        if (index.isValid())
            fileTree_->beginRenameEditor(index, request.initialName);
    } else {
        const QModelIndex parentIndex = fileSystemModel_->index(request.parentDirectory);
        if (parentIndex.isValid())
            fileTree_->beginCreateEditor(parentIndex,
                                         request.kind == ExplorerEditKind::NewDirectory);
    }
}

void MainWindow::hideExplorerCreateRow() { fileTree_->endInlineEditor(); }

void MainWindow::updateExplorerExclusions() {
    if (!fileSystemModel_ || !fileTree_ || !workbenchController_->hasWorkspace()) {
        return;
    }
    static const QSet<QString> generatedNames{QStringLiteral(".git"), QStringLiteral(".cache"),
                                              QStringLiteral("build"), QStringLiteral("out"),
                                              QStringLiteral("node_modules")};
    std::function<void(const QModelIndex&)> update = [&](const QModelIndex& parent) {
        for (int row = 0; row < fileSystemModel_->rowCount(parent); ++row) {
            const QModelIndex index = fileSystemModel_->index(row, 0, parent);
            const QFileInfo info = fileSystemModel_->fileInfo(index);
            const bool hidden = !showGeneratedFolders_ && info.isDir() &&
                                generatedNames.contains(info.fileName().toLower());
            fileTree_->setRowHidden(row, parent, hidden);
            if (!hidden && info.isDir()) {
                update(index);
            }
        }
    };
    update(fileTree_->rootIndex());
}

void MainWindow::saveCurrentFile() {
    QString error;
    if (!editorSessionController_->saveCurrent(&error)) {
        ConfirmationDialog::showMessage(this, tr("Save failed"), error);
    }
}

void MainWindow::saveCurrentFileAs() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save File As"),
                                                      workbenchController_->workspacePath());
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!editorSessionController_->saveCurrentAs(path, &error)) {
        ConfirmationDialog::showMessage(this, tr("Save failed"), error);
    }
}

void MainWindow::chooseTerminalFont() {
    const QFont initial(settings_.terminalFontFamily(), settings_.terminalFontSize());
    const QFont defaults(QStringLiteral("Consolas"), 11);
    EditorFontDialog dialog(initial, defaults, this, tr("Terminal Font"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QFont font = dialog.restoreDefaultsSelected() ? defaults : dialog.currentFont();
    settings_.saveTerminalFont(font.family(), font.pointSize());
    persistSettings();
    terminalPanel_->setTerminalFont(font);
    userSettingsSnapshot_.terminalFontFamily = settings_.terminalFontFamily();
    userSettingsSnapshot_.terminalFontSize = settings_.terminalFontSize();
}

void MainWindow::configureKeyboardShortcuts() {
    if (keyboardShortcutsDialog_ != nullptr) {
        keyboardShortcutsDialog_->showNormal();
        keyboardShortcutsDialog_->raise();
        keyboardShortcutsDialog_->activateWindow();
        return;
    }
    auto* dialog = new KeyboardShortcutsDialog(*commands_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    keyboardShortcutsDialog_ = dialog;
    connect(dialog, &KeyboardShortcutsDialog::shortcutChanged, this,
            [this](const QString& id, const QKeySequence& sequence) {
                settings_.saveCommandShortcut(id, sequence.toString(QKeySequence::PortableText));
                persistSettings();
                userSettingsSnapshot_.shortcuts = settings_.commandShortcutOverrides();
            });
    connect(dialog, &KeyboardShortcutsDialog::shortcutReset, this, [this](const QString& id) {
        settings_.removeCommandShortcut(id);
        persistSettings();
        userSettingsSnapshot_.shortcuts = settings_.commandShortcutOverrides();
    });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (keyboardShortcutsDialog_ == dialog)
            keyboardShortcutsDialog_ = nullptr;
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::applyTheme(const QString& theme) {
    applyThemeVisuals(theme);
    settings_.saveTheme(theme == QStringLiteral("light") ? QStringLiteral("light")
                                                         : QStringLiteral("dark"));
    persistSettings();
    userSettingsSnapshot_.theme = settings_.theme();
}

void MainWindow::applyThemeVisuals(const QString& theme) {
    const bool dark = theme != QStringLiteral("light");
    Theme::apply(*qApp, dark);
    updateThemeIcons(dark);
    if (searchResults_ != nullptr)
        searchResults_->viewport()->update();
    editorArea_->setDarkTheme(dark);
    if (windowOutline_ != nullptr)
        windowOutline_->setDarkTheme(dark);
    if (settingsDialog_ != nullptr)
        settingsDialog_->refreshTheme();
    if (keyboardShortcutsDialog_ != nullptr)
        keyboardShortcutsDialog_->refreshTheme();
}

void MainWindow::updateThemeIcons(bool dark) {
    qApp->setProperty("litecodeDarkTheme", dark);
    if (titleBar_ != nullptr) {
        titleBar_->setDarkTheme(dark);
    }
    refreshThemedIcons(this, dark);
    const auto themedActivityIcon = [dark](const QString& name) {
        const QString suffix = dark ? QString{} : QStringLiteral("-light");
        return workbenchActivityIcon(QStringLiteral(":/icons/%1%2.svg").arg(name, suffix),
                                     QStringLiteral(":/icons/%1-active%2.svg").arg(name, suffix));
    };
    if (explorerAction_) {
        explorerAction_->setIcon(themedActivityIcon(QStringLiteral("files")));
    }
    if (searchAction_) {
        searchAction_->setIcon(themedActivityIcon(QStringLiteral("search")));
    }
}

void MainWindow::chooseFile() {
    QString initialDirectory = workbenchController_->workspacePath();
    const QString currentFile = editorSessionController_->currentFile();
    if (!currentFile.isEmpty())
        initialDirectory = QFileInfo(currentFile).absolutePath();
    if (initialDirectory.isEmpty())
        initialDirectory = QDir::homePath();

    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open File"), initialDirectory, tr("All Files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!editorSessionController_->openFile(path, &error))
        ConfirmationDialog::showMessage(this, tr("Unable to open file"), error);
}

void MainWindow::chooseFileWithEncoding() {
    QString initialDirectory = workbenchController_->workspacePath();
    const QString currentFile = editorSessionController_->currentFile();
    if (!currentFile.isEmpty())
        initialDirectory = QFileInfo(currentFile).absolutePath();
    if (initialDirectory.isEmpty())
        initialDirectory = QDir::homePath();

    const QString path = QFileDialog::getOpenFileName(this, tr("Open File with Encoding"),
                                                      initialDirectory, tr("All Files (*)"));
    if (path.isEmpty())
        return;

    const QVector<core::TextEncoding> encodings = core::supportedTextDecodingEncodings();
    const std::optional<core::TextEncoding> encoding = chooseTextEncoding(
        path, tr("Select File Encoding to Open File"), encodings, std::nullopt, true);
    if (!encoding)
        return;

    QString error;
    if (!editorSessionController_->openFileWithEncoding(path, *encoding, &error))
        ConfirmationDialog::showMessage(this, tr("Unable to open file"), error);
}

void MainWindow::runWorkspaceSearch() {
    searchTimer_->stop();
    const bool hasQuery = !searchInput_->text().isEmpty();
    const bool hasWorkspace = workbenchController_->hasWorkspace();
    invalidateWorkspaceSearch(!hasWorkspace || !hasQuery);
    if (!hasWorkspace || !hasQuery) {
        return;
    }
    const QString searchRoot = workspaceSearchRoot_.isEmpty()
                                   ? workbenchController_->workspacePath()
                                   : workspaceSearchRoot_;
    const QString query = searchInput_->text();
    if (useRegexButton_->isChecked()) {
        const QRegularExpression expression(query);
        if (!expression.isValid()) {
            searchResults_->clear();
            auto* item = new QListWidgetItem(
                tr("Invalid regular expression: %1").arg(expression.errorString()), searchResults_);
            item->setFlags(Qt::NoItemFlags);
            return;
        }
    }
    activeSearchOperationId_ =
        workspaceSearch_
            ->searchText(searchRoot, query, workspace::defaultMaximumTextResults,
                         matchCaseButton_->isChecked(), matchWholeWordButton_->isChecked(),
                         useRegexButton_->isChecked(), settings_.fileEncoding())
            .value();
}

void MainWindow::invalidateWorkspaceSearch(bool clearVisibleResults) {
    activeSearchOperationId_ = 0;
    searchResultsReady_ = false;
    searchResultsTruncated_ = false;
    replaceAllWorkspaceButton_->setEnabled(false);
    if (clearVisibleResults)
        searchResults_->clear();
    currentSearchResults_.clear();
    currentSearchResultPaths_.clear();
    collapsedSearchFiles_.clear();
    workspaceSearch_->cancel();
}

void MainWindow::refreshWorkspaceSearch() {
    if (!workbenchController_->hasWorkspace() || searchInput_->text().isEmpty()) {
        runWorkspaceSearch();
        return;
    }
    // Keep the previous result tree visible until the replacement is ready.
    // runWorkspaceSearch invalidates the old operation before starting the new one.
    statusBar()->showMessage(tr("Refreshing search…"), 1500);
    runWorkspaceSearch();
}

void MainWindow::replaceWorkspaceMatches(const QString& targetPath, int targetLine,
                                         int targetColumn, int targetLength) {
    const QString query = searchInput_->text();
    if (!workbenchController_->hasWorkspace() || query.isEmpty() ||
        workspaceReplace_->isRunning() || !searchResultsReady_ || searchTimer_->isActive() ||
        currentSearchResults_.isEmpty())
        return;
    const bool replacingOne = !targetPath.isEmpty() && targetLine > 0;
    const bool replacingFile = !targetPath.isEmpty() && !replacingOne;
    const QString action = replacingOne    ? tr("Replace")
                           : replacingFile ? tr("Replace All in File")
                                           : tr("Replace All");
    QVector<workspace::WorkspaceReplaceMatch> matches;
    for (const workspace::SearchResult& result : currentSearchResults_) {
        if (!targetPath.isEmpty() && result.filePath != targetPath)
            continue;
        if (replacingOne && (result.line != targetLine || result.column != targetColumn ||
                             result.length != targetLength))
            continue;
        matches.push_back(
            {result.filePath, result.line, result.column, result.length, result.matchedText});
        if (replacingOne)
            break;
    }
    if (matches.isEmpty())
        return;
    const QStringList targets =
        targetPath.isEmpty() ? currentSearchResultPaths_ : QStringList{targetPath};
    for (const EditorDocumentInfo& document : editorArea_->documents()) {
        if (!document.modified)
            continue;
        const bool targetsDirtyDocument =
            std::any_of(targets.cbegin(), targets.cend(), [&document](const QString& target) {
                return isSameFilePath(target, document.filePath);
            });
        if (targetsDirtyDocument) {
            ConfirmationDialog::showMessage(
                this, tr("Replace unavailable"),
                tr("Save or discard changes in %1 before replacing workspace matches. "
                   "LiteCode will not overwrite an unsaved editor buffer.")
                    .arg(document.displayName));
            return;
        }
    }
    const QString confirmation =
        searchResultsTruncated_
            ? tr("Search results are incomplete. Replace only the %1 listed matches for “%2”?\n\n"
                 "This cannot be undone by LiteCode.")
                  .arg(matches.size())
                  .arg(query)
        : replacingFile ? tr("Replace the %1 listed matches for “%2” in this file?\n\n"
                             "This cannot be undone by LiteCode.")
                              .arg(matches.size())
                              .arg(query)
                        : tr("Replace the %1 listed matches for “%2” in this workspace?\n\n"
                             "This cannot be undone by LiteCode.")
                              .arg(matches.size())
                              .arg(query);
    if (!replacingOne && ConfirmationDialog::ask(this, action, confirmation, action, {},
                                                 tr("Cancel")) != ConfirmationChoice::Primary) {
        return;
    }
    const QString searchRoot = workspaceSearchRoot_.isEmpty()
                                   ? workbenchController_->workspacePath()
                                   : workspaceSearchRoot_;
    replaceAllWorkspaceButton_->setEnabled(false);
    workspace::WorkspaceReplaceRequest request;
    request.workspaceRoot = workbenchController_->workspacePath();
    request.rootPath = searchRoot;
    request.query = query;
    request.replacement = replaceInput_->text();
    request.matchCase = matchCaseButton_->isChecked();
    request.matchWholeWord = matchWholeWordButton_->isChecked();
    request.useRegularExpression = useRegexButton_->isChecked();
    request.preserveCase = preserveCaseButton_->isChecked();
    request.encoding = settings_.fileEncoding();
    request.targetMatches = std::move(matches);
    if (!workspaceReplace_->replaceAll(std::move(request)))
        replaceAllWorkspaceButton_->setEnabled(true);
}

void MainWindow::displaySearchResults(const QVector<workspace::SearchResult>& results) {
    searchResults_->setUpdatesEnabled(false);
    searchResults_->clear();
    currentSearchResults_ = results;
    currentSearchResultPaths_.clear();
    if (results.isEmpty()) {
        auto* item = new QListWidgetItem(tr("No results found"), searchResults_);
        item->setFlags(Qt::NoItemFlags);
        searchResults_->setUpdatesEnabled(true);
        return;
    }

    QMap<QString, QVector<int>> byFile;
    for (int index = 0; index < results.size(); ++index)
        byFile[results.at(index).filePath].push_back(index);
    currentSearchResultPaths_ = byFile.keys();
    auto* summary =
        new QListWidgetItem(tr("%1 result%2 in %3 file%4")
                                .arg(results.size())
                                .arg(results.size() == 1 ? QString{} : QStringLiteral("s"))
                                .arg(byFile.size())
                                .arg(byFile.size() == 1 ? QString{} : QStringLiteral("s")),
                            searchResults_);
    summary->setFlags(Qt::NoItemFlags);

    const QDir root(workbenchController_->workspacePath());
    for (auto it = byFile.cbegin(); it != byFile.cend(); ++it) {
        const QString path = it.key();
        auto* header = new QListWidgetItem(QFileInfo(path).fileName());
        header->setData(Qt::UserRole, path);
        header->setData(Qt::UserRole + 2, QStringLiteral("workspaceSearchFileHeader"));
        header->setData(Qt::UserRole + 6, collapsedSearchFiles_.contains(path));
        header->setData(Qt::UserRole + 7, QString::number(it.value().size()));
        const QString directory = QFileInfo(root.relativeFilePath(path)).path();
        header->setData(Qt::UserRole + 8, directory == QStringLiteral(".")
                                              ? QString{}
                                              : QDir::toNativeSeparators(directory));
        header->setToolTip(root.relativeFilePath(path));
        searchResults_->addItem(header);

        for (const int resultIndex : it.value()) {
            const workspace::SearchResult& result = results.at(resultIndex);
            auto* item = new QListWidgetItem(result.preview);
            item->setData(Qt::UserRole, result.filePath);
            item->setData(Qt::UserRole + 1, result.line);
            item->setData(Qt::UserRole + 2, QStringLiteral("workspaceSearchMatch"));
            item->setData(Qt::UserRole + 3, result.column);
            item->setData(Qt::UserRole + 4, result.length);
            item->setData(searchResultIndexRole, resultIndex);
            item->setToolTip(result.preview);
            searchResults_->addItem(item);
            if (collapsedSearchFiles_.contains(path))
                item->setHidden(true);
        }
    }
    searchResults_->setUpdatesEnabled(true);
}

void MainWindow::toggleSearchResultFile(QListWidgetItem* header) {
    const QString path = header->data(Qt::UserRole).toString();
    const bool collapsed = !collapsedSearchFiles_.contains(path);
    if (collapsed)
        collapsedSearchFiles_.insert(path);
    else
        collapsedSearchFiles_.remove(path);
    header->setData(Qt::UserRole + 6, collapsed);
    searchResults_->setUpdatesEnabled(false);
    const int first = searchResults_->row(header) + 1;
    for (int row = first; row < searchResults_->count(); ++row) {
        QListWidgetItem* item = searchResults_->item(row);
        if (item->data(Qt::UserRole + 2).toString() == QStringLiteral("workspaceSearchFileHeader"))
            break;
        item->setHidden(collapsed);
    }
    searchResults_->setUpdatesEnabled(true);
}
void MainWindow::closeEvent(QCloseEvent* event) {
    closing_ = true;
    if (!editorSessionController_->closeAllForApplicationExit()) {
        closing_ = false;
        event->ignore();
        return;
    }
    saveLayout();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (titleBar_ != nullptr)
            titleBar_->updateWindowState();
        if (windowOutline_ != nullptr)
            windowOutline_->syncToWindow(isMaximized());
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (postShowLayoutApplied_)
        return;

    postShowLayoutApplied_ = true;
    // Dock dimensions are not final until the native window has been shown. Reapply the saved
    // workbench state on the next event-loop turn so the sidebar and terminal keep their exact
    // visibility and size instead of being overwritten by Qt's initial layout pass.
    QTimer::singleShot(0, this, [this] {
        applyingSavedLayout_ = true;
        applySavedWorkbenchLayout();
        applyingSavedLayout_ = false;
    });
}

void MainWindow::paintEvent(QPaintEvent* event) {
    QMainWindow::paintEvent(event);
    if (!firstFrameSeen_) {
        firstFrameSeen_ = true;
        emit firstFramePresented();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (windowOutline_ != nullptr)
        windowOutline_->syncToWindow(isMaximized());
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (searchResults_ && watched == searchResults_->viewport() && replaceToggle_ &&
        replaceToggle_->isChecked() &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease)) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            mouse->position().x() >= searchResults_->viewport()->width() - 24) {
            if (QListWidgetItem* item = searchResults_->itemAt(mouse->position().toPoint())) {
                const QString kind = item->data(Qt::UserRole + 2).toString();
                if (kind == QStringLiteral("workspaceSearchFileHeader") ||
                    kind == QStringLiteral("workspaceSearchMatch")) {
                    if (event->type() == QEvent::MouseButtonRelease) {
                        const QString path = item->data(Qt::UserRole).toString();
                        const int line = kind == QStringLiteral("workspaceSearchMatch")
                                             ? item->data(Qt::UserRole + 1).toInt()
                                             : 0;
                        const int column = item->data(Qt::UserRole + 3).toInt();
                        const int length = item->data(Qt::UserRole + 4).toInt();
                        QTimer::singleShot(0, this, [this, path, line, column, length] {
                            replaceWorkspaceMatches(path, line, column, length);
                        });
                    }
                    return true;
                }
            }
        }
    }
    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    const bool belongsToThisWindow = watchedWidget != nullptr && watchedWidget->window() == this;
    const bool explorerInlineEditIsActive =
        explorerController_ != nullptr && explorerController_->activeEdit() != nullptr;
    const bool isExplorerWidget =
        watchedWidget != nullptr && fileTree_ != nullptr &&
        (watchedWidget == fileTree_ || fileTree_->isAncestorOf(watchedWidget));
    // QToolButtons deliberately keep focus on the current editor. Without handling the click
    // here, an inline explorer edit survives when the user switches views from the activity bar.
    // A pointer press anywhere else in this window has the same dismissing effect as a click in
    // the editor area.
    if (belongsToThisWindow && explorerInlineEditIsActive && !isExplorerWidget &&
        event->type() == QEvent::MouseButtonPress) {
        explorerController_->cancelEdit();
    }
    const auto pointerIsOverScrollBar = [](const QPoint& globalPosition) {
        for (QWidget* widget = QApplication::widgetAt(globalPosition); widget != nullptr;
             widget = widget->parentWidget()) {
            if (qobject_cast<QScrollBar*>(widget) != nullptr)
                return true;
        }
        return false;
    };
    const auto clearDockResizeCursor = [this] {
        if (dockResizeCursorTarget_ != nullptr)
            dockResizeCursorTarget_->unsetCursor();
        dockResizeCursorTarget_ = nullptr;
    };
    const auto resizeAxisAt = [this](const QPoint& globalPosition) {
        if (!workspaceDock_ || !bottomDock_)
            return DockResizeAxis::None;
        const QPoint localPosition = mapFromGlobal(globalPosition);
        constexpr int hitExtent = 5;
        const QRect workspaceGeometry = workspaceDock_->geometry();
        if (workspaceDock_->isVisible() && !workspaceDock_->isFloating() &&
            localPosition.y() >= workspaceGeometry.top() &&
            localPosition.y() <= workspaceGeometry.bottom() &&
            qAbs(localPosition.x() - workspaceGeometry.right()) <= hitExtent) {
            return DockResizeAxis::Horizontal;
        }
        const QRect bottomGeometry = bottomDock_->geometry();
        if (bottomDock_->isVisible() && !bottomDock_->isFloating() &&
            localPosition.x() >= bottomGeometry.left() &&
            localPosition.x() <= bottomGeometry.right() &&
            qAbs(localPosition.y() - bottomGeometry.top()) <= hitExtent) {
            return DockResizeAxis::Vertical;
        }
        return DockResizeAxis::None;
    };
    if (belongsToThisWindow &&
        (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease)) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() == Qt::LeftButton) {
            const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
            const DockResizeAxis axis = pointerIsOverScrollBar(globalPosition)
                                            ? DockResizeAxis::None
                                            : resizeAxisAt(globalPosition);
            if (axis != DockResizeAxis::None) {
                dockResizeAxis_ = axis;
                dockResizePressPosition_ = mouseEvent->globalPosition().toPoint();
                dockResizeInitialSize_ = axis == DockResizeAxis::Horizontal
                                             ? workspaceDock_->width()
                                             : bottomDock_->height();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove && dockResizeAxis_ != DockResizeAxis::None) {
            const QPoint position = mouseEvent->globalPosition().toPoint();
            if (dockResizeAxis_ == DockResizeAxis::Horizontal) {
                const int newWidth =
                    std::clamp(dockResizeInitialSize_ + position.x() - dockResizePressPosition_.x(),
                               160, std::max(160, width() - 160));
                resizeDocks({workspaceDock_}, {newWidth}, Qt::Horizontal);
            } else {
                const int newHeight =
                    std::clamp(dockResizeInitialSize_ - position.y() + dockResizePressPosition_.y(),
                               120, std::max(120, height() - 120));
                resizeDocks({bottomDock_}, {newHeight}, Qt::Vertical);
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && mouseEvent->button() == Qt::LeftButton &&
            dockResizeAxis_ != DockResizeAxis::None) {
            dockResizeAxis_ = DockResizeAxis::None;
            return true;
        }
        if (event->type() == QEvent::MouseMove && dockResizeAxis_ == DockResizeAxis::None) {
            const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
            const DockResizeAxis axis = pointerIsOverScrollBar(globalPosition)
                                            ? DockResizeAxis::None
                                            : resizeAxisAt(globalPosition);
            if (axis != DockResizeAxis::None) {
                if (dockResizeCursorTarget_ != watchedWidget)
                    clearDockResizeCursor();
                watchedWidget->setCursor(axis == DockResizeAxis::Horizontal ? Qt::SizeHorCursor
                                                                            : Qt::SizeVerCursor);
                dockResizeCursorTarget_ = watchedWidget;
            } else {
                clearDockResizeCursor();
            }
        }
    }
    if (watched == searchInput_ && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) &&
            !searchHistory_.isEmpty()) {
            if (keyEvent->key() == Qt::Key_Up) {
                searchHistoryIndex_ = qMin(searchHistoryIndex_ + 1, searchHistory_.size() - 1);
            } else {
                searchHistoryIndex_ = qMax(searchHistoryIndex_ - 1, -1);
            }
            searchInput_->setText(searchHistoryIndex_ < 0 ? QString{}
                                                          : searchHistory_.at(searchHistoryIndex_));
            searchInput_->selectAll();
            return true;
        }
    }
    const bool explorerEvent =
        watched == fileTree_ || (fileTree_ && watched == fileTree_->viewport());
    if (explorerEvent && event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            explorerPressedIndex_ =
                QPersistentModelIndex(fileTree_->indexAt(mouseEvent->position().toPoint()));
            explorerExpandedAtPress_ =
                explorerPressedIndex_.isValid() && fileTree_->isExpanded(explorerPressedIndex_);
            if (!explorerPressedIndex_.isValid() && watched == fileTree_->viewport() &&
                explorerController_->hasWorkspace()) {
                setExplorerRootSelected(true);
            }
        }
    } else if (explorerEvent && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPersistentModelIndex releasedIndex =
            fileTree_->indexAt(mouseEvent->position().toPoint());
        if (mouseEvent->button() == Qt::LeftButton && releasedIndex.isValid() &&
            releasedIndex == explorerPressedIndex_ &&
            fileSystemModel_->fileInfo(releasedIndex).isDir()) {
            const bool expandedBeforeClick = explorerExpandedAtPress_;
            QTimer::singleShot(0, fileTree_, [this, releasedIndex, expandedBeforeClick] {
                if (releasedIndex.isValid() &&
                    fileTree_->isExpanded(releasedIndex) == expandedBeforeClick) {
                    fileTree_->setExpanded(releasedIndex, !expandedBeforeClick);
                }
            });
        }
        explorerPressedIndex_ = QPersistentModelIndex{};
    }
    if (explorerEvent && event->type() == QEvent::KeyPress && !fileTree_->hasInlineEditor()) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Delete && keyEvent->modifiers() == Qt::NoModifier) {
            requestExplorerDelete();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const auto* nativeMessage = static_cast<MSG*>(message);
        if (nativeMessage->message == WM_GETMINMAXINFO) {
            // Frameless windows otherwise maximize to the monitor bounds and cover the
            // Windows taskbar. Constrain the native maximize rectangle to the monitor's
            // work area while retaining normal freeform resizing.
            auto* limits = reinterpret_cast<MINMAXINFO*>(nativeMessage->lParam);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            const HMONITOR monitor =
                MonitorFromWindow(nativeMessage->hwnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfo(monitor, &monitorInfo)) {
                const RECT& monitorRect = monitorInfo.rcMonitor;
                const RECT& workRect = monitorInfo.rcWork;
                limits->ptMaxPosition.x = workRect.left - monitorRect.left;
                limits->ptMaxPosition.y = workRect.top - monitorRect.top;
                limits->ptMaxSize.x = workRect.right - workRect.left;
                limits->ptMaxSize.y = workRect.bottom - workRect.top;
            }
            *result = 0;
            return true;
        }
        if (nativeMessage->message == WM_NCCALCSIZE && nativeMessage->wParam == TRUE) {
            auto* parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(nativeMessage->lParam);
            if (IsZoomed(nativeMessage->hwnd)) {
                MONITORINFO monitorInfo{};
                monitorInfo.cbSize = sizeof(monitorInfo);
                const HMONITOR monitor =
                    MonitorFromWindow(nativeMessage->hwnd, MONITOR_DEFAULTTONEAREST);
                if (GetMonitorInfo(monitor, &monitorInfo)) {
                    parameters->rgrc[0] = monitorInfo.rcWork;
                }
            }
            // Keep WS_THICKFRAME for native resizing, but do not let its invisible
            // non-client frame push LiteCode's custom chrome into the client area.
            *result = 0;
            return true;
        }
        if (nativeMessage->message == WM_SETCURSOR) {
            LPCTSTR cursorId = nullptr;
            switch (LOWORD(nativeMessage->lParam)) {
            case HTLEFT:
            case HTRIGHT:
                cursorId = IDC_SIZEWE;
                break;
            case HTTOP:
            case HTBOTTOM:
                cursorId = IDC_SIZENS;
                break;
            case HTTOPLEFT:
            case HTBOTTOMRIGHT:
                cursorId = IDC_SIZENWSE;
                break;
            case HTTOPRIGHT:
            case HTBOTTOMLEFT:
                cursorId = IDC_SIZENESW;
                break;
            default:
                break;
            }
            if (cursorId != nullptr) {
                SetCursor(LoadCursor(nullptr, cursorId));
                *result = TRUE;
                return true;
            }
        }
        if (nativeMessage->message == WM_NCHITTEST) {
            // WM_NCHITTEST carries native physical pixels while Qt widget geometry uses
            // device-independent coordinates. Convert through the HWND client space and its
            // effective DPI so title-bar controls remain clickable at every display scale.
            POINT nativePoint{static_cast<short>(LOWORD(nativeMessage->lParam)),
                              static_cast<short>(HIWORD(nativeMessage->lParam))};
            ScreenToClient(nativeMessage->hwnd, &nativePoint);
            const qreal scale = qMax(1.0, GetDpiForWindow(nativeMessage->hwnd) / 96.0);
            const QPoint localPosition(qRound(nativePoint.x / scale),
                                       qRound(nativePoint.y / scale));
            constexpr int resizeBorder = 7;
            if (!isMaximized()) {
                const bool left = localPosition.x() < resizeBorder;
                const bool right = localPosition.x() >= width() - resizeBorder;
                const bool top = localPosition.y() < resizeBorder;
                const bool bottom = localPosition.y() >= height() - resizeBorder;
                if (top && left) {
                    *result = HTTOPLEFT;
                    return true;
                }
                if (top && right) {
                    *result = HTTOPRIGHT;
                    return true;
                }
                if (bottom && left) {
                    *result = HTBOTTOMLEFT;
                    return true;
                }
                if (bottom && right) {
                    *result = HTBOTTOMRIGHT;
                    return true;
                }
                if (left || right || top || bottom) {
                    *result = left ? HTLEFT : right ? HTRIGHT : top ? HTTOP : HTBOTTOM;
                    return true;
                }
            }
            if (titleBar_ != nullptr && localPosition.y() < ThemeMetrics::navigationBarHeight &&
                titleBar_->containsInteractivePoint(titleBar_->mapFrom(this, localPosition))) {
                *result = HTCLIENT;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

} // namespace litecode::ui
