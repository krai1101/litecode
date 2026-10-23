#include "ui/EditorArea.h"
#include "core/FileSystemPath.h"
#include "core/SettingsService.h"

#include "editor/DocumentSession.h"
#include "editor/ScintillaEditor.h"
#include "ui/FileIconTheme.h"
#include "ui/PopupPositioner.h"
#include "ui/ThemedIcon.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/Controls.h"
#include "ui/components/EditorBreadcrumb.h"
#include "ui/components/EditorFindReplaceController.h"
#include "ui/components/FindTextInput.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace litecode::ui {
namespace {

QIcon editorFileIcon(const QString& path) { return fileIconForPath(path); }

struct BackgroundFindCount final {
    int total{};
    bool limitHit{};
    qint64 firstStart{-1};
    qint64 firstEnd{-1};
};

bool isWordCharacter(QChar character) {
    return character == QLatin1Char('_') || character.isLetterOrNumber();
}

bool invalidFindExpression(const QString& needle, bool regularExpression) {
    return regularExpression && !QRegularExpression(needle).isValid();
}

BackgroundFindCount countMatches(const QByteArray& utf8Text, const QString& needle, bool matchCase,
                                 bool wholeWord, bool regularExpression) {
    constexpr int maximumMatches = 19'999;
    const QString text = QString::fromUtf8(utf8Text);
    const Qt::CaseSensitivity sensitivity = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QRegularExpression expression;
    if (regularExpression) {
        expression.setPattern(needle);
        expression.setPatternOptions(matchCase ? QRegularExpression::NoPatternOption
                                               : QRegularExpression::CaseInsensitiveOption);
        if (!expression.isValid())
            return {};
    }
    int total = 0;
    qint64 firstStart = -1;
    qint64 firstEnd = -1;
    qsizetype position = 0;
    while (position <= text.size()) {
        const QRegularExpressionMatch match =
            regularExpression ? expression.match(text, position) : QRegularExpressionMatch{};
        const qsizetype found = regularExpression ? (match.hasMatch() ? match.capturedStart() : -1)
                                                  : text.indexOf(needle, position, sensitivity);
        if (found < 0)
            break;
        const qsizetype end = regularExpression ? match.capturedEnd() : found + needle.size();
        const bool validWord =
            !wholeWord || ((found == 0 || !isWordCharacter(text.at(found - 1))) &&
                           (end == text.size() || !isWordCharacter(text.at(end))));
        position = end > found ? end : found + 1;
        if (!validWord)
            continue;
        if (firstStart < 0) {
            firstStart = text.first(found).toUtf8().size();
            firstEnd = firstStart + text.sliced(found, end - found).toUtf8().size();
        }
        if (total == maximumMatches)
            return {maximumMatches, true, firstStart, firstEnd};
        ++total;
    }
    return {total, false, firstStart, firstEnd};
}

QString nearestExistingDirectory(const QString& filePath) {
    QDir directory = QFileInfo(filePath).absoluteDir();
    while (!directory.exists()) {
        const QString previous = directory.absolutePath();
        if (!directory.cdUp() || directory.absolutePath() == previous)
            return {};
    }
    return QDir::cleanPath(directory.absolutePath());
}

class EditorTabBar final : public QTabBar {
  public:
    explicit EditorTabBar(QWidget* parent = nullptr) : QTabBar(parent) {}

    void setDeleted(int index, bool deleted) {
        setTabData(index, deleted);
        update(tabRect(index));
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter painter(this);
        const QFont baseFont = font();
        for (int index = 0; index < count(); ++index) {
            QStyleOptionTab option;
            initStyleOption(&option, index);
            QFont font = baseFont;
            if (tabData(index).toBool())
                font.setStrikeOut(true);
            painter.setFont(font);
            painter.drawControl(QStyle::CE_TabBarTab, option);
        }
    }
};

class EditorTabWidget final : public QTabWidget {
  public:
    explicit EditorTabWidget(QWidget* parent = nullptr) : QTabWidget(parent) {
        setTabBar(new EditorTabBar(this));
    }
};

} // namespace

struct EditorArea::Page {
    std::unique_ptr<editor::DocumentSession> document;
    QWidget* container{};
    QWidget* breadcrumb{};
    QHBoxLayout* breadcrumbLayout{};
    editor::ScintillaEditor* editor{};
    QLabel* loadingLabel{};
    QLabel* dirtyIndicator{};
    QFrame* reloadErrorBanner{};
    QLabel* reloadErrorLabel{};
    QToolButton* tabClose{};
    int pendingLine{-1};
    int pendingColumn{};
    int pendingLength{};
    bool documentAnnounced{false};
    QString reloadError;
};

EditorArea::EditorArea(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("editorArea"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    findBar_ = new QFrame(this);
    findBar_->setObjectName(QStringLiteral("findReplaceBar"));
    auto* findLayout = new QGridLayout(findBar_);
    findLayout->setContentsMargins(9, 4, 4, 4);
    findLayout->setHorizontalSpacing(3);
    findLayout->setVerticalSpacing(4);
    replaceToggle_ = new components::IconButton(findBar_);
    replaceToggle_->setObjectName(QStringLiteral("findToggleButton"));
    replaceToggle_->setCheckable(true);
    replaceToggle_->setToolTip(tr("Show Replace"));
    findInputFrame_ = new QFrame(findBar_);
    findInputFrame_->setObjectName(QStringLiteral("findInputFrame"));
    auto* findInputLayout = new QHBoxLayout(findInputFrame_);
    findInputLayout->setContentsMargins(0, 0, 2, 0);
    findInputLayout->setSpacing(2);
    findInput_ = new components::FindTextInput(findInputFrame_);
    findInput_->setObjectName(QStringLiteral("findInput"));
    findInput_->setPlaceholderText(tr("Find"));
    findInputLayout->addWidget(findInput_, 1);
    replaceInput_ = new components::FindTextInput(findBar_);
    replaceInput_->setObjectName(QStringLiteral("replaceInput"));
    replaceInput_->setPlaceholderText(tr("Replace"));
    findInput_->installEventFilter(this);
    replaceInput_->installEventFilter(this);
    matchCase_ = new components::IconButton(findInputFrame_);
    matchCase_->setObjectName(QStringLiteral("findOptionButton"));
    matchCase_->setText(QStringLiteral("Aa"));
    matchCase_->setCheckable(true);
    matchCase_->setToolTip(tr("Match Case"));
    wholeWord_ = new components::IconButton(findInputFrame_);
    wholeWord_->setObjectName(QStringLiteral("findOptionButton"));
    wholeWord_->setText(QStringLiteral("ab"));
    wholeWord_->setCheckable(true);
    wholeWord_->setToolTip(tr("Match Whole Word"));
    regularExpression_ = new components::IconButton(findInputFrame_);
    regularExpression_->setObjectName(QStringLiteral("findOptionButton"));
    regularExpression_->setText(QStringLiteral(".*"));
    regularExpression_->setCheckable(true);
    regularExpression_->setToolTip(tr("Use Regular Expression"));
    findInputLayout->addWidget(matchCase_);
    findInputLayout->addWidget(wholeWord_);
    findInputLayout->addWidget(regularExpression_);
    findResultLabel_ = new QLabel(tr("No results"), findBar_);
    findResultLabel_->setObjectName(QStringLiteral("findResultLabel"));
    findResultLabel_->setFixedWidth(69);
    findResultLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* previous = new components::IconButton(findBar_);
    previous->setObjectName(QStringLiteral("findActionButton"));
    setThemedIcon(previous, QStringLiteral(":/icons/arrow-up.svg"));
    previous->setToolTip(tr("Find Previous"));
    auto* next = new components::IconButton(findBar_);
    next->setObjectName(QStringLiteral("findActionButton"));
    setThemedIcon(next, QStringLiteral(":/icons/arrow-down.svg"));
    next->setToolTip(tr("Find Next (Enter)"));
    findSelectionButton_ = new components::IconButton(findBar_);
    findSelectionButton_->setObjectName(QStringLiteral("findSelectionButton"));
    setThemedIcon(findSelectionButton_, QStringLiteral(":/icons/find-selection.svg"));
    findSelectionButton_->setCheckable(true);
    findSelectionButton_->setToolTip(tr("Find in Selection"));
    findSelectionButton_->setEnabled(false);
    replaceButton_ = new components::IconButton(findBar_);
    replaceButton_->setObjectName(QStringLiteral("findActionButton"));
    setThemedIcon(replaceButton_, QStringLiteral(":/icons/replace.svg"));
    replaceButton_->setToolTip(tr("Replace"));
    replaceAllButton_ = new components::IconButton(findBar_);
    replaceAllButton_->setObjectName(QStringLiteral("findActionButton"));
    setThemedIcon(replaceAllButton_, QStringLiteral(":/icons/replace-all.svg"));
    replaceAllButton_->setToolTip(tr("Replace All"));
    // This is an icon-only action. A QPushButton with both text and an icon paints a second,
    // smaller cross beside the themed close icon on some platform styles.
    auto* close = new components::IconButton(findBar_);
    close->setObjectName(QStringLiteral("findActionButton"));
    setThemedIcon(close, QStringLiteral(":/icons/close.svg"));
    close->setToolTip(tr("Close Find"));
    findLayout->addWidget(replaceToggle_, 0, 0, 2, 1);
    findLayout->addWidget(findInputFrame_, 0, 1);
    findLayout->addWidget(findResultLabel_, 0, 2);
    findLayout->addWidget(previous, 0, 3);
    findLayout->addWidget(next, 0, 4);
    findLayout->addWidget(findSelectionButton_, 0, 5);
    findLayout->addWidget(close, 0, 6);
    findLayout->addWidget(replaceInput_, 1, 1);
    findLayout->addWidget(replaceButton_, 1, 3);
    findLayout->addWidget(replaceAllButton_, 1, 4);
    findLayout->setColumnStretch(1, 1);
    findBar_->hide();
    editorStack_ = new QStackedWidget(this);
    welcomePage_ = new QWidget(editorStack_);
    auto* welcomeLayout = new QVBoxLayout(welcomePage_);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    auto* brand = new QLabel(QStringLiteral("LiteCode"), welcomePage_);
    brand->setObjectName(QStringLiteral("welcomeBrand"));
    brand->setAlignment(Qt::AlignCenter);
    auto* tagline = new QLabel(tr("Open a folder or file to begin"), welcomePage_);
    tagline->setObjectName(QStringLiteral("welcomeTagline"));
    tagline->setAlignment(Qt::AlignCenter);
    auto* shortcuts = new QWidget(welcomePage_);
    shortcuts->setObjectName(QStringLiteral("welcomeShortcuts"));
    auto* shortcutLayout = new QGridLayout(shortcuts);
    shortcutLayout->setContentsMargins(0, 0, 0, 0);
    shortcutLayout->setHorizontalSpacing(28);
    shortcutLayout->setVerticalSpacing(6);
    const auto addShortcut = [shortcuts, shortcutLayout](int row, const QString& action,
                                                         const QString& keybinding) {
        auto* actionLabel = new QLabel(action, shortcuts);
        actionLabel->setObjectName(QStringLiteral("welcomeAction"));
        auto* keybindingLabel = new QLabel(keybinding, shortcuts);
        keybindingLabel->setObjectName(QStringLiteral("welcomeKeybinding"));
        shortcutLayout->addWidget(actionLabel, row, 0);
        shortcutLayout->addWidget(keybindingLabel, row, 1, Qt::AlignRight);
    };
    addShortcut(0, tr("Open Folder"), tr("Ctrl+K, Ctrl+O"));
    addShortcut(1, tr("Open File"), tr("Ctrl+O"));
    welcomeLayout->addWidget(brand);
    welcomeLayout->addWidget(tagline);
    welcomeLayout->addWidget(shortcuts, 0, Qt::AlignHCenter);

    tabs_ = new EditorTabWidget(editorStack_);
    tabs_->setObjectName(QStringLiteral("editorTabs"));
    tabs_->setAccessibleName(tr("Open editors"));
    // Qt's native tab close button is platform styled (and becomes an orange
    // boxed button on Windows). Editor tabs use the workbench's neutral close
    // icon so the affordance stays consistent in both themes.
    tabs_->setTabsClosable(false);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    tabs_->setIconSize(QSize(17, 17));
    tabs_->tabBar()->setObjectName(QStringLiteral("editorTabBar"));
    tabs_->tabBar()->setUsesScrollButtons(true);
    tabs_->tabBar()->setMouseTracking(true);
    tabs_->tabBar()->installEventFilter(this);
    tabOverflowScroll_ = new QScrollBar(Qt::Horizontal, tabs_->tabBar());
    tabOverflowScroll_->setObjectName(QStringLiteral("editorTabOverflowScroll"));
    tabOverflowScroll_->setFocusPolicy(Qt::NoFocus);
    tabOverflowScroll_->setSingleStep(1);
    tabOverflowScroll_->hide();
    editorStack_->addWidget(welcomePage_);
    editorStack_->addWidget(tabs_);
    editorStack_->setCurrentWidget(welcomePage_);
    layout->addWidget(editorStack_);
    findBar_->raise();
    watcher_ = new QFileSystemWatcher(this);
    externalReloadTimer_ = new QTimer(this);
    externalReloadTimer_->setSingleShot(true);
    externalReloadTimer_->setInterval(150);
    findCountTimer_ = new QTimer(this);
    findCountTimer_->setSingleShot(true);
    findCountTimer_->setInterval(120);

    findReplaceController_ = new components::EditorFindReplaceController(
        {*findBar_, *findInput_, *previous, *next, *replaceButton_, *replaceAllButton_, *close,
         *replaceToggle_, [this] { findNext(); }, [this] { handleFindInputChanged(); },
         [this] { findPrevious(); }, [this] { replaceNext(); }, [this] { replaceAll(); },
         [this](bool visible) { setReplaceVisible(visible); }},
        this);
    connect(close, &QAbstractButton::clicked, this, &EditorArea::closeFindReplace);
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, &EditorArea::handleExternalChange);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { handleExternalDirectoryChange(); });
    connect(externalReloadTimer_, &QTimer::timeout, this,
            &EditorArea::reloadPendingExternalChanges);
    connect(findCountTimer_, &QTimer::timeout, this, &EditorArea::refreshFindCount);
    connect(matchCase_, &QToolButton::toggled, this, [this] { handleFindInputChanged(); });
    connect(wholeWord_, &QToolButton::toggled, this, [this] { handleFindInputChanged(); });
    connect(regularExpression_, &QToolButton::toggled, this, [this] { handleFindInputChanged(); });
    connect(replaceInput_, &QPlainTextEdit::textChanged, this, &EditorArea::updateFindBarGeometry);
    connect(findSelectionButton_, &QToolButton::toggled, this, [this](bool enabled) {
        if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor) {
            page->editor->setFindScope(
                enabled ? std::optional<QPair<qint64, qint64>>(findSelectionRange_) : std::nullopt);
            handleFindInputChanged();
        }
    });

    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (const Page* page = pageAt(index))
            emit documentCloseRequested(page->document->id());
    });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        const auto* page = pageAt(index);
        {
            const QSignalBlocker blocker(findSelectionButton_);
            findSelectionButton_->setChecked(false);
            findSelectionButton_->setEnabled(false);
        }
        findSelectionRange_ = {};
        for (const auto& openPage : pages_) {
            if (openPage->editor) {
                openPage->editor->setFindScope(std::nullopt);
                openPage->editor->setFindMode(findBar_->isVisible() && openPage.get() == page);
            }
        }
        updateTabOverflowScrollBar();
        tabOverflowPosition_ =
            qBound(tabOverflowScroll_->minimum(), index, tabOverflowScroll_->maximum());
        const QSignalBlocker blocker(tabOverflowScroll_);
        tabOverflowScroll_->setValue(tabOverflowPosition_);
        updateTabCloseButtons();
        emit currentFileChanged(page ? page->document->filePath() : QString{});
        emit currentEncodingChanged(page ? page->document->encoding() : core::TextEncoding::Utf8);
        const bool canReplace = page && page->editor && !page->document->isReadOnly();
        replaceToggle_->setEnabled(canReplace);
        if (!canReplace && replaceVisible_)
            setReplaceVisible(false);
        scheduleFindRefresh();
    });
    connect(tabOverflowScroll_, &QScrollBar::valueChanged, this, [this](int position) {
        const int steps = position - tabOverflowPosition_;
        if (steps == 0)
            return;

        cacheTabScrollButtons();
        QToolButton* scrollButton = steps < 0 ? tabScrollLeft_ : tabScrollRight_;
        if (scrollButton) {
            for (int step = 0; step < std::abs(steps); ++step)
                scrollButton->click();
        }
        tabOverflowPosition_ = position;
    });
    connect(tabs_->tabBar(), &QTabBar::tabMoved, this,
            [this](int, int) { updateTabOverflowScrollBar(); });
}

EditorArea::~EditorArea() = default;

bool EditorArea::eventFilter(QObject* watched, QEvent* event) {
    if (watched == findInput_ &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        QWidget* frame = findInput_->parentWidget();
        frame->setProperty("focused", event->type() == QEvent::FocusIn);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
    }
    if ((watched == findInput_ || watched == replaceInput_) && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            if (static_cast<components::FindTextInput*>(watched)->text().contains(
                    QLatin1Char('\n')))
                return false;
            const bool older = key->key() == Qt::Key_Up;
            if (watched == findInput_)
                return cycleFindHistory(findInput_, findHistory_, findHistoryIndex_,
                                        findHistoryDraft_, older);
            return cycleFindHistory(replaceInput_, replaceHistory_, replaceHistoryIndex_,
                                    replaceHistoryDraft_, older);
        }
        if (key->key() == Qt::Key_Escape) {
            closeFindReplace();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (key->modifiers().testFlag(Qt::ControlModifier)) {
                static_cast<components::FindTextInput*>(watched)->insertPlainText(
                    QStringLiteral("\n"));
                updateFindBarGeometry();
                return true;
            }
            if (watched == findInput_)
                rememberFindHistory(findInput_, findHistory_);
            else
                rememberFindHistory(replaceInput_, replaceHistory_);
            if (watched == findInput_) {
                if (key->modifiers().testFlag(Qt::ShiftModifier))
                    findPrevious();
                else
                    findNext();
                return true;
            }
            if (watched == replaceInput_) {
                replaceNext();
                return true;
            }
        }
    }
    if (watched == tabs_->tabBar()) {
        if (event->type() == QEvent::MouseMove) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            updateTabCloseButtons(tabs_->tabBar()->tabAt(mouse->position().toPoint()));
        } else if (event->type() == QEvent::Leave) {
            updateTabCloseButtons();
        } else if (event->type() == QEvent::Wheel) {
            const auto* wheel = static_cast<QWheelEvent*>(event);
            const int delta =
                !wheel->angleDelta().isNull() ? wheel->angleDelta().y() : wheel->pixelDelta().y();
            if (delta == 0 || !tabOverflowScroll_->isVisible())
                return false;

            const int next =
                qBound(tabOverflowScroll_->minimum(), tabOverflowPosition_ + (delta > 0 ? -1 : 1),
                       tabOverflowScroll_->maximum());
            if (next != tabOverflowPosition_)
                tabOverflowScroll_->setValue(next);
            return true;
        } else if (event->type() == QEvent::ContextMenu) {
            const auto* context = static_cast<QContextMenuEvent*>(event);
            const int index = tabs_->tabBar()->tabAt(context->pos());
            if (index >= 0) {
                showTabContextMenu(index, context->globalPos());
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EditorArea::showTabContextMenu(int index, const QPoint& globalPosition) {
    Page* selectedPage = pageAt(index);
    if (selectedPage == nullptr)
        return;

    tabs_->setCurrentIndex(index);
    const core::DocumentId selectedId = selectedPage->document->id();
    const QString selectedPath = selectedPage->document->filePath();

    components::Menu menu(tabs_->tabBar());
    QAction* close = menu.addAction(tr("Close"));
    close->setShortcut(QKeySequence::Close);
    QAction* closeOthers = menu.addAction(tr("Close Others"));
    closeOthers->setEnabled(pages_.size() > 1);
    QAction* closeSaved = menu.addAction(tr("Close Saved"));
    closeSaved->setEnabled(std::any_of(pages_.cbegin(), pages_.cend(), [](const auto& page) {
        return !page->document->isModified();
    }));
    QAction* closeAll = menu.addAction(tr("Close All"));

    menu.addSeparator();
    QAction* copyPath = menu.addAction(tr("Copy Path"));
    QAction* copyRelativePath = menu.addAction(tr("Copy Relative Path"));
    QString relativePath;
    if (!workspaceRoot_.isEmpty()) {
        relativePath = QDir(workspaceRoot_).relativeFilePath(selectedPath);
        if (relativePath == QStringLiteral("..") || relativePath.startsWith(QStringLiteral("../")))
            relativePath.clear();
    }
    copyRelativePath->setEnabled(!relativePath.isEmpty());

    menu.addSeparator();
    QAction* revealInExplorerView = menu.addAction(tr("Reveal in Explorer View"));
    QAction* revealInFileExplorer = menu.addAction(tr("Reveal in File Explorer"));

    QAction* chosen = PopupPositioner::execMenuAt(menu, globalPosition, window());
    if (chosen == nullptr)
        return;
    if (chosen == copyPath) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(selectedPath));
        return;
    }
    if (chosen == copyRelativePath) {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(relativePath));
        return;
    }
    if (chosen == revealInExplorerView) {
        emit revealFileRequested(selectedPath);
        return;
    }
    if (chosen == revealInFileExplorer) {
        emit revealInFileExplorerRequested(selectedPath);
        return;
    }

    QVector<core::DocumentId> documentsToClose;
    if (chosen == close) {
        documentsToClose.append(selectedId);
    } else if (chosen == closeOthers) {
        for (const auto& page : pages_) {
            if (page->document->id() != selectedId)
                documentsToClose.append(page->document->id());
        }
    } else if (chosen == closeSaved) {
        for (const auto& page : pages_) {
            if (!page->document->isModified())
                documentsToClose.append(page->document->id());
        }
    } else if (chosen == closeAll) {
        for (const auto& page : pages_)
            documentsToClose.append(page->document->id());
    }
    for (const core::DocumentId documentId : documentsToClose)
        emit documentCloseRequested(documentId);
}

void EditorArea::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateFindBarGeometry();
    updateTabOverflowScrollBar();
}

void EditorArea::updateTabOverflowScrollBar() {
    QTabBar* tabBar = tabs_->tabBar();
    cacheTabScrollButtons();
    int totalWidth = 0;
    for (int index = 0; index < tabBar->count(); ++index)
        totalWidth += tabBar->tabRect(index).width();

    const bool overflow = totalWidth > tabBar->width();
    tabOverflowScroll_->setVisible(overflow);
    if (!overflow)
        return;

    const int maximum = qMax(0, tabBar->count() - 1);
    const int visibleTabs = qMax(1, (tabBar->width() * tabBar->count()) / totalWidth);
    tabOverflowScroll_->setGeometry(0, tabBar->height() - 4, tabBar->width(), 4);
    tabOverflowScroll_->setRange(0, maximum);
    tabOverflowScroll_->setPageStep(visibleTabs);
    tabOverflowPosition_ = qBound(0, tabOverflowPosition_, maximum);
    const QSignalBlocker blocker(tabOverflowScroll_);
    tabOverflowScroll_->setValue(tabOverflowPosition_);
    tabOverflowScroll_->raise();
}

void EditorArea::cacheTabScrollButtons() {
    for (QToolButton* button : tabs_->tabBar()->findChildren<QToolButton*>()) {
        if (button->arrowType() == Qt::LeftArrow)
            tabScrollLeft_ = button;
        else if (button->arrowType() == Qt::RightArrow)
            tabScrollRight_ = button;
    }

    for (QToolButton* button : {tabScrollLeft_, tabScrollRight_}) {
        if (!button)
            continue;
        button->setArrowType(Qt::NoArrow);
        button->setFixedSize(0, 0);
        button->hide();
    }
}

bool EditorArea::openFile(const QString& filePath, QString* errorMessage,
                          std::optional<core::TextEncoding> requestedEncoding) {
    const QFileInfo info(filePath);
    const int openIndex = indexOfPath(info.absoluteFilePath());
    if (openIndex >= 0) {
        tabs_->setCurrentIndex(openIndex);
        if (requestedEncoding)
            return reopenCurrentWithEncoding(*requestedEncoding, errorMessage);
        return true;
    }
    if (!info.exists() || !info.isFile()) {
        if (errorMessage) {
            *errorMessage = tr("The selected file does not exist or is not a regular file.");
        }
        return false;
    }

    const QString resolvedPath =
        info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    const int existingIndex = indexOfPath(resolvedPath);
    if (existingIndex >= 0) {
        tabs_->setCurrentIndex(existingIndex);
        if (requestedEncoding)
            return reopenCurrentWithEncoding(*requestedEncoding, errorMessage);
        return true;
    }

    auto page = std::make_unique<Page>();
    const core::DocumentId documentId = documentIds_.next();
    if (!documentId.isValid()) {
        if (errorMessage)
            *errorMessage = tr("LiteCode exhausted its document identifier space.");
        return false;
    }
    page->document = std::make_unique<editor::DocumentSession>(documentId, resolvedPath);
    page->container = new QWidget(tabs_);
    auto* pageLayout = new QVBoxLayout(page->container);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    page->loadingLabel = new QLabel(tr("Opening %1...").arg(info.fileName()), page->container);
    page->loadingLabel->setObjectName(QStringLiteral("editorLoadingLabel"));
    page->loadingLabel->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(page->loadingLabel, 1);
    const int tabIndex =
        tabs_->addTab(page->container, editorFileIcon(resolvedPath), info.fileName());
    auto* tabActions = new QWidget(tabs_->tabBar());
    tabActions->setObjectName(QStringLiteral("editorTabActions"));
    tabActions->setFixedSize(24, 24);
    auto* tabActionsLayout = new QHBoxLayout(tabActions);
    tabActionsLayout->setContentsMargins(0, 0, 0, 0);
    tabActionsLayout->setSpacing(0);
    tabActionsLayout->setAlignment(Qt::AlignCenter);
    auto* dirtyIndicator = new QLabel(QStringLiteral("\u25CF"), tabActions);
    dirtyIndicator->setObjectName(QStringLiteral("editorTabDirtyIndicator"));
    dirtyIndicator->setAlignment(Qt::AlignCenter);
    dirtyIndicator->setFixedSize(16, 24);
    dirtyIndicator->hide();
    auto* tabClose = new components::IconButton(tabActions);
    tabClose->setObjectName(QStringLiteral("editorTabClose"));
    tabClose->setAutoRaise(true);
    tabClose->setFixedSize(24, 24);
    tabClose->setIconSize(QSize(19, 19));
    const QString closeLabel = tr("Close %1").arg(info.fileName());
    tabClose->setToolTip(closeLabel);
    tabClose->setAccessibleName(closeLabel);
    setThemedIcon(tabClose, QStringLiteral(":/icons/close.svg"));
    tabActionsLayout->addWidget(dirtyIndicator);
    tabActionsLayout->addWidget(tabClose);
    tabs_->tabBar()->setTabButton(tabIndex, QTabBar::RightSide, tabActions);
    page->dirtyIndicator = dirtyIndicator;
    page->tabClose = tabClose;
    connect(tabClose, &QToolButton::clicked, this,
            [this, documentId] { emit documentCloseRequested(documentId); });
    pages_.push_back(std::move(page));
    tabs_->setCurrentIndex(tabIndex);
    updateTabCloseButtons();
    editorStack_->setCurrentWidget(tabs_);
    emit currentFileChanged(resolvedPath);
    emit openFilesChanged();

    auto* loadWatcher = new QFutureWatcher<editor::DocumentLoadResult>(this);
    connect(loadWatcher, &QFutureWatcher<editor::DocumentLoadResult>::finished, this,
            [this, loadWatcher, documentId] {
                const editor::DocumentLoadResult load = loadWatcher->result();
                loadWatcher->deleteLater();
                finishOpen(documentId, load);
            });
    const core::TextEncoding defaultEncoding = defaultEncoding_;
    const bool autoGuessEncoding = autoGuessEncoding_;
    loadWatcher->setFuture(
        QtConcurrent::run([resolvedPath, requestedEncoding, defaultEncoding, autoGuessEncoding] {
            return editor::DocumentSession::loadBounded(resolvedPath, requestedEncoding,
                                                        defaultEncoding, autoGuessEncoding);
        }));
    return true;
}

void EditorArea::finishOpen(core::DocumentId documentId, const editor::DocumentLoadResult& load) {
    Page* page = pageForDocument(documentId);
    if (!page)
        return;

    if (!editor::DocumentSession::matchesDiskSnapshot(page->document->filePath(), load) ||
        !page->document->acceptLoad(load)) {
        const QString failedPath = page->document->filePath();
        const QString diagnostic = load.outcome == editor::DocumentLoadResult::Outcome::Loaded
                                       ? tr("The file changed while it was opening. Open it again.")
                                   : load.diagnostic.isEmpty() ? tr("The file could not be opened.")
                                                               : load.diagnostic;
        const int index = tabs_->indexOf(page->container);
        closePage(index);
        emit fileOpenFailed(failedPath, diagnostic);
        return;
    }

    initializeEditor(*page, load.contents);
    page->documentAnnounced = true;
    watch(*page);
    updateTabTitle(*page);
    if (page->document->isDeleted() && QFileInfo::exists(page->document->filePath()))
        scheduleAutomaticReload(*page);
    emit documentOpened(documentId, page->document->filePath(), load.contents,
                        page->document->version(), page->document->storageMode());
    if (page == pageAt(tabs_->currentIndex()))
        emit currentEncodingChanged(page->document->encoding());
    if (page == pageAt(tabs_->currentIndex()))
        replaceToggle_->setEnabled(!page->document->isReadOnly());
    if (page == pageAt(tabs_->currentIndex()))
        scheduleFindRefresh();
    if (page->pendingLine >= 0) {
        if (page->pendingColumn > 0 && page->pendingLength > 0)
            page->editor->goToMatch(page->pendingLine, page->pendingColumn, page->pendingLength);
        else
            page->editor->goToLine(page->pendingLine);
        page->pendingLine = -1;
        page->pendingColumn = 0;
        page->pendingLength = 0;
    }
}

void EditorArea::initializeEditor(Page& page, const QByteArray& contents) {
    auto* pageLayout = qobject_cast<QVBoxLayout*>(page.container->layout());
    if (page.loadingLabel) {
        pageLayout->removeWidget(page.loadingLabel);
        page.loadingLabel->deleteLater();
        page.loadingLabel = nullptr;
    }
    page.breadcrumb = new QWidget(page.container);
    page.breadcrumb->setObjectName(QStringLiteral("editorBreadcrumb"));
    page.breadcrumbLayout = new QHBoxLayout(page.breadcrumb);
    page.breadcrumbLayout->setContentsMargins(12, 0, 12, 0);
    page.breadcrumbLayout->setSpacing(4);
    page.editor = new editor::ScintillaEditor(page.container);
    // Unlike side panels, editor scrollbars stay visible whenever the document needs them.
    TransientScrollBars::installIn(page.editor, true);
    page.editor->setLargeFileMode(page.document->storageMode() == core::DocumentStorageMode::Large);
    page.editor->setFilePath(page.document->filePath());
    page.editor->setLineEnding(page.document->lineEnding());
    page.editor->setEditorFont(fontFamily_, fontPixelSize_, fontWeight_);
    page.editor->setIndentWidth(indentWidth_);
    page.editor->setDarkTheme(darkTheme_);
    page.editor->setText(contents);
    page.editor->setFindMode(findBar_->isVisible() && &page == pageAt(tabs_->currentIndex()));
    page.editor->setReadOnly(page.document->isReadOnly());
    connect(page.editor, &editor::ScintillaEditor::contextMenuRequested, this,
            [this, editor = page.editor](const QPoint& position) {
                components::Menu menu(editor);
                menu.setObjectName(QStringLiteral("editorContextMenu"));
                QAction* cutAction = menu.addAction(tr("Cut"), QKeySequence::Cut, editor,
                                                    &editor::ScintillaEditor::cut);
                QAction* copyAction = menu.addAction(tr("Copy"), QKeySequence::Copy, editor,
                                                     &editor::ScintillaEditor::copy);
                menu.addAction(tr("Paste"), QKeySequence::Paste, editor,
                               &editor::ScintillaEditor::paste);
                const bool hasSelection = !editor->selectedText().isEmpty();
                cutAction->setEnabled(hasSelection);
                copyAction->setEnabled(hasSelection);
                (void)PopupPositioner::execMenuAt(menu, editor->mapToGlobal(position), window());
            });
    page.reloadErrorBanner = new QFrame(page.container);
    page.reloadErrorBanner->setObjectName(QStringLiteral("editorReloadErrorBanner"));
    page.reloadErrorBanner->setFrameShape(QFrame::StyledPanel);
    auto* errorLayout = new QHBoxLayout(page.reloadErrorBanner);
    errorLayout->setContentsMargins(10, 5, 6, 5);
    page.reloadErrorLabel = new QLabel(page.reloadErrorBanner);
    page.reloadErrorLabel->setWordWrap(true);
    auto* dismissError = new components::IconButton(page.reloadErrorBanner);
    dismissError->setToolTip(tr("Dismiss"));
    dismissError->setFixedSize(24, 24);
    setThemedIcon(dismissError, QStringLiteral(":/icons/close.svg"));
    errorLayout->addWidget(page.reloadErrorLabel, 1);
    errorLayout->addWidget(dismissError);
    connect(dismissError, &QToolButton::clicked, page.reloadErrorBanner, &QWidget::hide);
    page.reloadErrorBanner->hide();
    pageLayout->addWidget(page.breadcrumb);
    pageLayout->addWidget(page.reloadErrorBanner);
    pageLayout->addWidget(page.editor, 1);
    updateBreadcrumb(page);
    if (page.document->storageMode() == core::DocumentStorageMode::Large)
        emit largeFileModeActivated(page.document->filePath());

    const core::DocumentId documentId = page.document->id();
    connect(page.editor, &editor::EditorBackend::modificationChanged, this,
            [this, documentId](bool modified) {
                Page* current = pageForDocument(documentId);
                if (!current)
                    return;
                current->document->setModified(modified);
                updateTabTitle(*current);
            });
    connect(page.editor, &editor::EditorBackend::textChanged, this, [this, documentId] {
        const Page* current = pageAt(tabs_->currentIndex());
        if (current && current->document->id() == documentId)
            scheduleFindRefresh();
    });
    connect(page.editor, &editor::EditorBackend::textEdited, this,
            [this, documentId](qint64 position, qint64 removedLength,
                               const QByteArray& insertedText, const core::TextRange& range) {
                Page* current = pageForDocument(documentId);
                if (!current)
                    return;
                core::DocumentEdit edit{documentId,   {},    position, removedLength,
                                        insertedText, range, 0};
                if (current->document->applyEdit(&edit))
                    emit documentEdited(edit);
            });
    connect(page.editor, &editor::EditorBackend::cursorPositionChanged, this,
            [this, documentId](int line, int column) {
                Page* current = pageForDocument(documentId);
                if (current && current == pageAt(tabs_->currentIndex()))
                    emit cursorPositionChanged(line, column);
            });
}

bool EditorArea::saveCurrentAs(const QString& filePath, QString* errorMessage) {
    Page* page = pageAt(tabs_->currentIndex());
    if (!page) {
        return true;
    }
    if (!page->editor) {
        if (errorMessage)
            *errorMessage = tr("The document is still loading.");
        return false;
    }
    const QFileInfo destinationInfo(filePath);
    const QString destination = destinationInfo.canonicalFilePath().isEmpty()
                                    ? destinationInfo.absoluteFilePath()
                                    : destinationInfo.canonicalFilePath();
    const int destinationIndex = indexOfPath(destination);
    if (destinationIndex >= 0 && pageAt(destinationIndex) != page) {
        if (errorMessage)
            *errorMessage = tr("That file is already open in another editor tab.");
        return false;
    }
    const QString oldPath = page->document->filePath();
    core::DocumentPathChange pathChange;
    if (!page->document->saveAs(destination, page->editor->text(), &pathChange, errorMessage)) {
        return false;
    }
    watcher_->removePath(oldPath);
    page->editor->setFilePath(page->document->filePath());
    page->editor->setReadOnly(page->document->isReadOnly());
    page->editor->markSaved();
    watch(*page);
    refreshDeletedDirectoryWatches();
    updateTabTitle(*page);
    updateBreadcrumb(*page);
    emit currentFileChanged(page->document->filePath());
    emit documentPathChanged(pathChange);
    emit openFilesChanged();
    return true;
}

void EditorArea::rebindPaths(const QString& previousPath, const QString& path) {
    const QString previous = core::normalizedFileSystemPath(previousPath);
    const QString replacement = core::normalizedFileSystemPath(path);
    bool changed = false;
    for (const auto& ownedPage : pages_) {
        Page& page = *ownedPage;
        const QString current = core::normalizedFileSystemPath(page.document->filePath());
        QString rebound;
        const Qt::CaseSensitivity sensitivity = core::fileSystemCaseSensitivity(current);
        if (core::pathsReferToSameEntry(current, previous)) {
            rebound = replacement;
        } else if (current.startsWith(previous + QLatin1Char('/'), sensitivity)) {
            rebound = replacement + current.sliced(previous.size());
        } else {
            continue;
        }

        watcher_->removePath(page.document->filePath());
        core::DocumentPathChange pathChange;
        page.document->rebindPath(rebound, &pathChange);
        if (page.editor)
            page.editor->setFilePath(page.document->filePath());
        watch(page);
        updateTabTitle(page);
        updateBreadcrumb(page);
        emit documentPathChanged(pathChange);
        changed = true;
    }
    if (!changed)
        return;
    emit currentFileChanged(currentFile());
    emit openFilesChanged();
}

bool EditorArea::saveDocument(core::DocumentId documentId, QString* errorMessage) {
    Page* page = pageForDocument(documentId);
    if (!page) {
        if (errorMessage)
            *errorMessage = tr("The document is no longer open.");
        return false;
    }
    const int index = tabs_->indexOf(page->container);
    if (index >= 0)
        tabs_->setCurrentIndex(index);
    return saveCurrent(errorMessage);
}

bool EditorArea::reloadDocument(core::DocumentId documentId, QString* errorMessage) {
    Page* page = pageForDocument(documentId);
    if (!page || !page->editor) {
        if (errorMessage)
            *errorMessage = tr("The document is no longer available for reload.");
        return false;
    }

    const editor::DocumentLoadResult load = editor::DocumentSession::loadBounded(
        page->document->filePath(), page->document->preferredEncoding(),
        page->document->defaultEncoding(), autoGuessEncoding_);
    return finishReload(*page, load, errorMessage);
}

bool EditorArea::reopenCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage) {
    Page* page = pageAt(tabs_->currentIndex());
    if (!page || !page->editor) {
        if (errorMessage)
            *errorMessage = tr("The document is no longer available for reload.");
        return false;
    }
    if (page->document->isModified()) {
        if (errorMessage)
            *errorMessage =
                tr("Save or discard the unsaved changes before reopening with another encoding.");
        return false;
    }
    const editor::DocumentLoadResult load =
        editor::DocumentSession::loadBounded(page->document->filePath(), encoding);
    return finishReload(*page, load, errorMessage);
}

bool EditorArea::finishReload(Page& page, const editor::DocumentLoadResult& load,
                              QString* errorMessage) {
    if (!editor::DocumentSession::matchesDiskSnapshot(page.document->filePath(), load) ||
        !page.document->acceptReload(load)) {
        if (errorMessage)
            *errorMessage = load.outcome == editor::DocumentLoadResult::Outcome::Loaded
                                ? tr("The file changed again while it was reloading. Try again.")
                                : load.diagnostic;
        watch(page);
        return false;
    }

    const core::DocumentId documentId = page.document->id();
    const QString filePath = page.document->filePath();
    const qint64 version = page.document->version();
    if (page.documentAnnounced)
        emit documentClosed(documentId, filePath, version - 1);
    const QSignalBlocker blocker(page.editor);
    page.editor->setLargeFileMode(page.document->storageMode() == core::DocumentStorageMode::Large);
    page.editor->setLineEnding(page.document->lineEnding());
    page.editor->reloadText(load.contents);
    page.editor->setReadOnly(page.document->isReadOnly());
    page.reloadError.clear();
    if (page.reloadErrorBanner)
        page.reloadErrorBanner->hide();
    page.editor->markSaved();
    updateTabTitle(page);
    updateBreadcrumb(page);
    watch(page);
    refreshDeletedDirectoryWatches();
    page.documentAnnounced = true;
    emit documentOpened(documentId, filePath, load.contents, version, page.document->storageMode());
    if (&page == pageAt(tabs_->currentIndex()))
        emit currentEncodingChanged(page.document->encoding());
    if (&page == pageAt(tabs_->currentIndex()))
        scheduleFindRefresh();
    return true;
}

bool EditorArea::closeDocument(core::DocumentId documentId) {
    Page* page = pageForDocument(documentId);
    return page ? closePage(tabs_->indexOf(page->container)) : true;
}

void EditorArea::markDocumentDeleted(core::DocumentId documentId) {
    if (Page* page = pageForDocument(documentId))
        markExternallyDeleted(*page);
}

void EditorArea::resumeWatching(core::DocumentId documentId) {
    if (Page* page = pageForDocument(documentId))
        watch(*page);
}

QVector<EditorDocumentInfo> EditorArea::documents() const {
    QVector<EditorDocumentInfo> result;
    result.reserve(static_cast<qsizetype>(pages_.size()));
    for (const auto& page : pages_) {
        result.push_back({page->document->id(), page->document->filePath(),
                          page->document->displayName(), page->document->version(),
                          page->document->isModified(), page->editor != nullptr,
                          page->document->encoding()});
    }
    return result;
}

void EditorArea::showFindReplace(bool replaceVisible) {
    const Page* page = pageAt(tabs_->currentIndex());
    if (page && page->editor) {
        if (!findBar_->isVisible()) {
            findSelectionRange_ = page->editor->selectionByteRange();
            findSelectionButton_->setEnabled(findSelectionRange_.first <
                                             findSelectionRange_.second);
        }
        QString seed = page->editor->selectedText();
        if (seed.isEmpty())
            seed = page->editor->wordAtCaret();
        if (!seed.isEmpty())
            findInput_->setText(seed);
    }
    const bool canReplace = page && page->editor && !page->document->isReadOnly();
    replaceToggle_->setEnabled(canReplace);
    setReplaceVisible(replaceVisible && canReplace);
    findBar_->show();
    for (const auto& openPage : pages_) {
        if (openPage->editor)
            openPage->editor->setFindMode(openPage.get() == page);
    }
    findBar_->raise();
    updateFindBarGeometry();
    findInput_->setFocus();
    findInput_->selectAll();
    scheduleFindRefresh();
}

void EditorArea::setReplaceVisible(bool visible) {
    const Page* page = pageAt(tabs_->currentIndex());
    visible = visible && page && page->editor && !page->document->isReadOnly();
    replaceVisible_ = visible;
    const QSignalBlocker blocker(replaceToggle_);
    replaceToggle_->setChecked(visible);
    replaceToggle_->setToolTip(visible ? tr("Hide Replace") : tr("Show Replace"));
    // Use the same Codicon-style chevrons as the workspace search control. Text glyphs vary
    // between fonts and made the compact editor find widget look unlike the VS Code control.
    replaceToggle_->setText({});
    replaceToggle_->setIconSize(QSize(16, 16));
    const QString chevron = visible ? QStringLiteral(":/icons/chevron-down.svg")
                                    : QStringLiteral(":/icons/chevron-right.svg");
    setThemedIcon(replaceToggle_, chevron);
    replaceInput_->setVisible(visible);
    replaceButton_->setVisible(visible);
    replaceAllButton_->setVisible(visible);
    updateFindBarGeometry();
}

void EditorArea::updateFindBarGeometry() {
    if (!findBar_ || !findInput_ || !replaceInput_)
        return;
    const int lineHeight = QFontMetrics(findInput_->font()).lineSpacing();
    const int findExtra = (qMin(5, findInput_->document()->blockCount()) - 1) * lineHeight;
    const int replaceExtra = (qMin(5, replaceInput_->document()->blockCount()) - 1) * lineHeight;
    findInputFrame_->setFixedHeight(23 + findExtra);
    findInput_->setFixedHeight(21 + findExtra);
    replaceInput_->setFixedHeight(23 + replaceExtra);
    findBar_->setFixedWidth(qMax(160, qMin(width() - 24, 419)));
    findBar_->setFixedHeight(35 + findExtra + (replaceVisible_ ? 29 + replaceExtra : 0));
    findBar_->move(qMax(8, width() - findBar_->width() - 14), 38);
}

void EditorArea::setEditorFont(const QString& family, int pixelSize, int weight) {
    fontFamily_ = family;
    fontPixelSize_ = qBound(6, pixelSize, 72);
    fontWeight_ = weight == QFont::DemiBold || weight == QFont::Bold ? weight : QFont::Normal;
    for (const auto& page : pages_) {
        if (page->editor)
            page->editor->setEditorFont(fontFamily_, fontPixelSize_, fontWeight_);
    }
}

void EditorArea::resetEditorZoom() {
    for (const auto& page : pages_) {
        if (page->editor)
            page->editor->resetZoom();
    }
}

void EditorArea::setIndentWidth(int width) {
    indentWidth_ = qBound(1, width, 16);
    for (const auto& page : pages_) {
        if (page->editor)
            page->editor->setIndentWidth(indentWidth_);
    }
}

void EditorArea::setDefaultEncoding(core::TextEncoding encoding) { defaultEncoding_ = encoding; }

void EditorArea::setAutoGuessEncoding(bool enabled) { autoGuessEncoding_ = enabled; }

void EditorArea::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    for (const auto& page : pages_) {
        if (page->editor) {
            page->editor->setDarkTheme(dark);
            updateBreadcrumb(*page);
        }
    }
}

bool EditorArea::saveCurrent(QString* errorMessage) {
    Page* page = pageAt(tabs_->currentIndex());
    if (!page) {
        return true;
    }
    if (!page->editor) {
        if (errorMessage)
            *errorMessage = tr("The document is still loading.");
        return false;
    }
    if (!page->document->isModified() && !page->editor->isModified())
        return true;
    watcher_->removePath(page->document->filePath());
    if (!page->document->save(page->editor->text(), errorMessage)) {
        watch(*page);
        return false;
    }
    page->editor->markSaved();
    page->editor->setReadOnly(page->document->isReadOnly());
    page->document->setModified(false);
    watch(*page);
    refreshDeletedDirectoryWatches();
    updateTabTitle(*page);
    return true;
}

bool EditorArea::saveCurrentWithEncoding(core::TextEncoding encoding, QString* errorMessage) {
    Page* page = pageAt(tabs_->currentIndex());
    if (!page || !page->editor) {
        if (errorMessage)
            *errorMessage = tr("The document is still loading.");
        return false;
    }
    watcher_->removePath(page->document->filePath());
    if (!page->document->saveWithEncoding(page->editor->text(), encoding, errorMessage)) {
        watch(*page);
        return false;
    }
    page->editor->markSaved();
    page->editor->setReadOnly(page->document->isReadOnly());
    page->document->setModified(false);
    watch(*page);
    refreshDeletedDirectoryWatches();
    updateTabTitle(*page);
    emit currentEncodingChanged(page->document->encoding());
    return true;
}

QStringList EditorArea::openFiles() const {
    QStringList files;
    files.reserve(static_cast<qsizetype>(pages_.size()));
    for (const auto& page : pages_) {
        files.push_back(page->document->filePath());
    }
    return files;
}

QString EditorArea::currentFile() const {
    const Page* page = pageAt(tabs_->currentIndex());
    return page ? page->document->filePath() : QString{};
}

core::TextEncoding EditorArea::currentEncoding() const {
    const Page* page = pageAt(tabs_->currentIndex());
    return page ? page->document->encoding() : core::TextEncoding::Utf8;
}

QString EditorArea::selectedText() const {
    const Page* page = pageAt(tabs_->currentIndex());
    return page && page->editor ? page->editor->selectedText() : QString{};
}

void EditorArea::goToLine(int line) {
    if (Page* page = pageAt(tabs_->currentIndex())) {
        if (page->editor)
            page->editor->goToLine(line);
        else {
            page->pendingLine = line;
            page->pendingColumn = 0;
            page->pendingLength = 0;
        }
    }
}

void EditorArea::goToMatch(int line, int column, int length) {
    if (Page* page = pageAt(tabs_->currentIndex())) {
        if (page->editor)
            page->editor->goToMatch(line, column, length);
        else {
            page->pendingLine = line;
            page->pendingColumn = column;
            page->pendingLength = length;
        }
    }
}

void EditorArea::selectAll() {
    if (Page* page = pageAt(tabs_->currentIndex())) {
        if (page->editor)
            page->editor->selectAll();
    }
}

void EditorArea::setWorkspaceRoot(const QString& rootPath) {
    workspaceRoot_ = QDir::cleanPath(rootPath);
    if (historySettings_) {
        findHistory_ =
            historySettings_->searchHistory(workspaceRoot_, QStringLiteral("editorFind"));
        replaceHistory_ =
            historySettings_->searchHistory(workspaceRoot_, QStringLiteral("editorReplace"));
        findHistoryIndex_ = -1;
        replaceHistoryIndex_ = -1;
    }
    for (const auto& page : pages_) {
        if (page->editor)
            updateBreadcrumb(*page);
    }
}

void EditorArea::setHistorySettings(core::SettingsService* settings) {
    historySettings_ = settings;
    if (historySettings_) {
        findHistory_ =
            historySettings_->searchHistory(workspaceRoot_, QStringLiteral("editorFind"));
        replaceHistory_ =
            historySettings_->searchHistory(workspaceRoot_, QStringLiteral("editorReplace"));
    }
}

void EditorArea::rememberFindHistory(components::FindTextInput* input, QStringList& history) {
    const QString entry = input->text();
    if (entry.isEmpty())
        return;
    history.removeAll(entry);
    history.prepend(entry);
    if (history.size() > 50)
        history.resize(50);
    if (&history == &findHistory_)
        findHistoryIndex_ = -1;
    else
        replaceHistoryIndex_ = -1;
    if (historySettings_)
        historySettings_->saveSearchHistory(workspaceRoot_,
                                            &history == &findHistory_
                                                ? QStringLiteral("editorFind")
                                                : QStringLiteral("editorReplace"),
                                            history);
}

bool EditorArea::cycleFindHistory(components::FindTextInput* input, QStringList& history,
                                  int& index, QString& draft, bool older) {
    if (history.isEmpty())
        return false;
    if (index < 0)
        draft = input->text();
    index =
        older ? std::min(index + 1, static_cast<int>(history.size()) - 1) : std::max(index - 1, -1);
    input->setText(index < 0 ? draft : history.at(index));
    return true;
}

EditorArea::Page* EditorArea::pageAt(int index) const {
    if (index < 0 || index >= tabs_->count()) {
        return nullptr;
    }
    auto* widget = tabs_->widget(index);
    const auto found = std::find_if(pages_.begin(), pages_.end(), [widget](const auto& page) {
        return page->container == widget;
    });
    return found == pages_.end() ? nullptr : found->get();
}

EditorArea::Page* EditorArea::pageForDocument(core::DocumentId documentId) const {
    const auto found = std::find_if(pages_.begin(), pages_.end(), [documentId](const auto& page) {
        return page->document->id() == documentId;
    });
    return found == pages_.end() ? nullptr : found->get();
}

int EditorArea::indexOfPath(const QString& filePath) const {
    for (const auto& page : pages_) {
        if (core::pathsReferToSameEntry(page->document->filePath(), filePath)) {
            return tabs_->indexOf(page->container);
        }
    }
    return -1;
}

void EditorArea::updateTabTitle(Page& page) {
    const int index = tabs_->indexOf(page.container);
    if (index < 0) {
        return;
    }
    QTabBar* tabBar = tabs_->tabBar();
    const bool deleted = page.document->isDeleted();
    tabBar->setTabText(index, page.document->displayName());
    static_cast<EditorTabBar*>(tabBar)->setDeleted(index, deleted);
    updateTabCloseButtons();
    updateTabOverflowScrollBar();
    QString toolTip = page.document->filePath();
    if (page.document->isReadOnly())
        toolTip += tr("\nRead-only");
    if (!page.reloadError.isEmpty())
        toolTip += tr("\nReload failed: %1").arg(page.reloadError);
    tabs_->setTabToolTip(index, toolTip);
}

void EditorArea::updateBreadcrumb(Page& page) {
    QString path = QDir::fromNativeSeparators(page.document->filePath());
    if (!workspaceRoot_.isEmpty()) {
        const QString relative = QDir(workspaceRoot_).relativeFilePath(path);
        if (!relative.startsWith(QStringLiteral(".."))) {
            path = relative;
        }
    }
    components::rebuildEditorBreadcrumb(*page.breadcrumbLayout, *page.breadcrumb, path,
                                        page.document->filePath(), darkTheme_,
                                        page.document->isDeleted());
}

void EditorArea::updateTabCloseButtons(int hoveredTab) {
    for (int index = 0; index < tabs_->count(); ++index) {
        Page* page = pageAt(index);
        if (!page)
            continue;
        const bool hovered = index == hoveredTab;
        const bool dirty = page->document->isModified();
        if (page->dirtyIndicator)
            page->dirtyIndicator->setVisible(dirty && !hovered);
        if (page->tabClose)
            page->tabClose->setVisible(hovered || (!dirty && index == tabs_->currentIndex()));
    }
}

bool EditorArea::closePage(int index) {
    Page* page = pageAt(index);
    if (!page) {
        return true;
    }

    QWidget* widget = tabs_->widget(index);
    const core::DocumentId documentId = page->document->id();
    const QString closedPath = page->document->filePath();
    const qint64 finalVersion = page->document->version();
    const bool documentAnnounced = page->documentAnnounced;
    watcher_->removePath(closedPath);
    if (page->editor)
        disconnect(page->editor, nullptr, this, nullptr);
    page->document->close();
    tabs_->removeTab(index);
    updateTabOverflowScrollBar();
    const auto found = std::find_if(pages_.begin(), pages_.end(), [page](const auto& candidate) {
        return candidate.get() == page;
    });
    if (found != pages_.end()) {
        pages_.erase(found);
    }
    refreshDeletedDirectoryWatches();
    widget->deleteLater();
    if (documentAnnounced)
        emit documentClosed(documentId, closedPath, finalVersion);
    emit openFilesChanged();
    if (tabs_->count() == 0) {
        editorStack_->setCurrentWidget(welcomePage_);
    }
    return true;
}

void EditorArea::findNext() {
    if (invalidFindExpression(findInput_->text(), regularExpression_->isChecked())) {
        findResultLabel_->setText(tr("Invalid regex"));
        return;
    }
    if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor) {
        const bool found =
            page->editor->findNext(findInput_->text(), matchCase_->isChecked(),
                                   wholeWord_->isChecked(), regularExpression_->isChecked());
        updateFindResult(found);
    }
}

void EditorArea::handleFindInputChanged() {
    updateFindBarGeometry();
    scheduleFindRefresh(true);
    Page* page = pageAt(tabs_->currentIndex());
    if (!page || !page->editor || findInput_->text().isEmpty()) {
        if (page && page->editor)
            page->editor->clearFindHighlights();
        findResultLabel_->setText(tr("No results"));
        return;
    }
    if (invalidFindExpression(findInput_->text(), regularExpression_->isChecked())) {
        findCountTimer_->stop();
        page->editor->clearFindHighlights();
        findResultLabel_->setText(tr("Invalid regex"));
        return;
    }
    if (page->editor->isLargeFileMode()) {
        findResultLabel_->setText(QStringLiteral("…"));
        findCountTimer_->start();
        return;
    }
    const bool found =
        page->editor->findNext(findInput_->text(), matchCase_->isChecked(), wholeWord_->isChecked(),
                               regularExpression_->isChecked());
    if (!found) {
        (void)page->editor->updateFindHighlights(findInput_->text(), matchCase_->isChecked(),
                                                 wholeWord_->isChecked(),
                                                 regularExpression_->isChecked());
        findResultLabel_->setText(tr("No results"));
        return;
    }
    findResultLabel_->setText(tr("1 of …"));
    findCountTimer_->start();
}

void EditorArea::scheduleFindRefresh(bool moveCursor) {
    findCountTimer_->stop();
    ++findCountGeneration_;
    findMoveCursor_ = moveCursor;
    largeFindNeedle_.clear();
    if (!findBar_->isVisible())
        return;
    const Page* page = pageAt(tabs_->currentIndex());
    if (!page || !page->editor || findInput_->text().isEmpty()) {
        findResultLabel_->setText(tr("No results"));
        return;
    }
    if (moveCursor)
        findSelectionAtRequest_ = page->editor->selectionByteRange();
    findResultLabel_->setText(QStringLiteral("…"));
    findCountTimer_->start();
}

void EditorArea::refreshFindCount() {
    if (invalidFindExpression(findInput_->text(), regularExpression_->isChecked())) {
        findResultLabel_->setText(tr("Invalid regex"));
        return;
    }
    Page* page = pageAt(tabs_->currentIndex());
    if (!page || !page->editor || findInput_->text().isEmpty()) {
        findResultLabel_->setText(tr("No results"));
        return;
    }
    if (!page->editor->isLargeFileMode()) {
        updateFindResult(true);
        return;
    }

    const quint64 generation = findCountGeneration_;
    const core::DocumentId documentId = page->document->id();
    const quint64 contentRevision = page->editor->contentRevision();
    const bool moveCursor = findMoveCursor_;
    const QPair<qint64, qint64> selectionAtRequest = findSelectionAtRequest_;
    const auto scope = page->editor->findScope();
    const qint64 scopeStart = scope ? scope->first : 0;
    const QByteArray contents =
        scope ? page->editor->text().mid(scopeStart, scope->second - scopeStart)
              : page->editor->text();
    const QString needle = findInput_->text();
    const bool matchCase = matchCase_->isChecked();
    const bool wholeWord = wholeWord_->isChecked();
    const bool regularExpression = regularExpression_->isChecked();
    auto* watcher = new QFutureWatcher<BackgroundFindCount>(this);
    connect(watcher, &QFutureWatcher<BackgroundFindCount>::finished, this,
            [this, watcher, generation, documentId, contentRevision, moveCursor, selectionAtRequest,
             scopeStart, scope, needle, matchCase, wholeWord, regularExpression] {
                const BackgroundFindCount count = watcher->result();
                watcher->deleteLater();
                const Page* current = pageAt(tabs_->currentIndex());
                if (generation != findCountGeneration_ || !current ||
                    current->document->id() != documentId || findInput_->text() != needle ||
                    current->editor->contentRevision() != contentRevision ||
                    current->editor->findScope() != scope || matchCase_->isChecked() != matchCase ||
                    wholeWord_->isChecked() != wholeWord ||
                    regularExpression_->isChecked() != regularExpression)
                    return;
                if (count.total == 0) {
                    findResultLabel_->setText(tr("No results"));
                    return;
                }
                largeFindNeedle_ = needle;
                largeFindTotal_ = count.total;
                largeFindLimitHit_ = count.limitHit;
                largeFindMatchCase_ = matchCase;
                largeFindWholeWord_ = wholeWord;
                largeFindRegularExpression_ = regularExpression;
                const bool shouldMoveCursor =
                    moveCursor && current->editor->selectionByteRange() == selectionAtRequest;
                if (shouldMoveCursor && count.firstStart >= 0)
                    current->editor->selectByteRange(scopeStart + count.firstStart,
                                                     scopeStart + count.firstEnd);
                const QString total = QString::number(count.total) +
                                      (count.limitHit ? QStringLiteral("+") : QString{});
                findResultLabel_->setText(shouldMoveCursor ? tr("1 of %1").arg(total)
                                                           : tr("%1 results").arg(total));
            });
    watcher->setFuture(
        QtConcurrent::run([contents, needle, matchCase, wholeWord, regularExpression] {
            return countMatches(contents, needle, matchCase, wholeWord, regularExpression);
        }));
}

void EditorArea::findPrevious() {
    if (invalidFindExpression(findInput_->text(), regularExpression_->isChecked())) {
        findResultLabel_->setText(tr("Invalid regex"));
        return;
    }
    if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor) {
        const bool found =
            page->editor->findPrevious(findInput_->text(), matchCase_->isChecked(),
                                       wholeWord_->isChecked(), regularExpression_->isChecked());
        updateFindResult(found);
    }
}

void EditorArea::updateFindResult(bool found) {
    Page* page = pageAt(tabs_->currentIndex());
    if (!found || !page || !page->editor) {
        findResultLabel_->setText(tr("No results"));
        return;
    }
    if (page->editor->isLargeFileMode()) {
        if (largeFindNeedle_ != findInput_->text() ||
            largeFindMatchCase_ != matchCase_->isChecked() ||
            largeFindWholeWord_ != wholeWord_->isChecked() ||
            largeFindRegularExpression_ != regularExpression_->isChecked()) {
            refreshFindCount();
            return;
        }
        const QString total = QString::number(largeFindTotal_) +
                              (largeFindLimitHit_ ? QStringLiteral("+") : QString{});
        findResultLabel_->setText(tr("? of %1").arg(total));
        return;
    }
    const editor::FindMatchStatus status = page->editor->updateFindHighlights(
        findInput_->text(), matchCase_->isChecked(), wholeWord_->isChecked(),
        regularExpression_->isChecked());
    if (status.total == 0) {
        findResultLabel_->setText(tr("No results"));
        return;
    }
    const QString total =
        QString::number(status.total) + (status.limitHit ? QStringLiteral("+") : QString{});
    findResultLabel_->setText(status.current > 0 ? tr("%1 of %2").arg(status.current).arg(total)
                                                 : tr("%1 results").arg(total));
}

void EditorArea::closeFindReplace() {
    rememberFindHistory(findInput_, findHistory_);
    if (replaceVisible_)
        rememberFindHistory(replaceInput_, replaceHistory_);
    findBar_->hide();
    {
        const QSignalBlocker blocker(findSelectionButton_);
        findSelectionButton_->setChecked(false);
        findSelectionButton_->setEnabled(false);
    }
    findSelectionRange_ = {};
    for (const auto& page : pages_) {
        if (page->editor) {
            page->editor->setFindMode(false);
            page->editor->setFindScope(std::nullopt);
        }
    }
    if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor)
        page->editor->setFocus();
}

void EditorArea::undo() {
    if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor)
        page->editor->undo();
}

void EditorArea::redo() {
    if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor)
        page->editor->redo();
}

void EditorArea::replaceNext() {
    if (invalidFindExpression(findInput_->text(), regularExpression_->isChecked())) {
        findResultLabel_->setText(tr("Invalid regex"));
        return;
    }
    if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor) {
        const bool replaced = page->editor->replaceNext(
            findInput_->text(), replaceInput_->text(), matchCase_->isChecked(),
            wholeWord_->isChecked(), regularExpression_->isChecked());
        updateFindResult(replaced);
    }
}

void EditorArea::replaceAll() {
    if (invalidFindExpression(findInput_->text(), regularExpression_->isChecked())) {
        findResultLabel_->setText(tr("Invalid regex"));
        return;
    }
    if (Page* page = pageAt(tabs_->currentIndex()); page && page->editor) {
        const int count = page->editor->replaceAll(findInput_->text(), replaceInput_->text(),
                                                   matchCase_->isChecked(), wholeWord_->isChecked(),
                                                   regularExpression_->isChecked());
        findResultLabel_->setText(tr("Replaced %1").arg(count));
    }
}

void EditorArea::handleExternalChange(const QString& filePath) {
    const int index = indexOfPath(filePath);
    Page* page = pageAt(index);
    if (!page || !page->editor) {
        return;
    }
    if (!QFileInfo::exists(filePath)) {
        scheduleDeletionConfirmation(page->document->id(), filePath);
        return;
    }
    if (page->document->isModified()) {
        emit externalModificationDetected(
            {page->document->id(), filePath, page->document->displayName(), true});
        return;
    }
    scheduleAutomaticReload(*page);
}

void EditorArea::scheduleDeletionConfirmation(core::DocumentId documentId,
                                              const QString& filePath) {
    QTimer::singleShot(
        100, this, [this, documentId, filePath] { confirmExternalDeletion(documentId, filePath); });
}

void EditorArea::confirmExternalDeletion(core::DocumentId documentId, const QString& filePath) {
    Page* page = pageForDocument(documentId);
    if (!page || !core::pathsReferToSameEntry(page->document->filePath(), filePath))
        return;
    if (QFileInfo::exists(filePath)) {
        watch(*page);
        if (page->document->isModified())
            emit externalModificationDetected(
                {documentId, filePath, page->document->displayName(), true});
        else
            scheduleAutomaticReload(*page);
        return;
    }
    markExternallyDeleted(*page);
}

void EditorArea::markExternallyDeleted(Page& page) {
    page.document->markDeleted();
    watcher_->removePath(page.document->filePath());
    updateTabTitle(page);
    updateBreadcrumb(page);
    refreshDeletedDirectoryWatches();
}

void EditorArea::showReloadError(Page& page, const QString& message) {
    page.reloadError = message;
    if (page.reloadErrorLabel)
        page.reloadErrorLabel->setText(
            tr("Unable to reload %1: %2").arg(page.document->displayName(), message));
    if (page.reloadErrorBanner)
        page.reloadErrorBanner->show();
    updateTabTitle(page);
}

void EditorArea::handleExternalDirectoryChange() {
    for (const auto& ownedPage : pages_) {
        Page& page = *ownedPage;
        if (!page.document->isDeleted() || !QFileInfo::exists(page.document->filePath()))
            continue;
        if (page.document->isModified()) {
            page.document->markRestored();
            updateTabTitle(page);
            updateBreadcrumb(page);
            watch(page);
            emit externalModificationDetected({page.document->id(), page.document->filePath(),
                                               page.document->displayName(), true});
        } else {
            scheduleAutomaticReload(page);
        }
    }
    refreshDeletedDirectoryWatches();
}

void EditorArea::refreshDeletedDirectoryWatches() {
    QSet<QString> desired;
    for (const auto& page : pages_) {
        if (!page->document->isDeleted())
            continue;
        const QString directory = nearestExistingDirectory(page->document->filePath());
        if (!directory.isEmpty())
            desired.insert(directory);
    }
    const QStringList watchedDirectories = watcher_->directories();
    const QSet<QString> current(watchedDirectories.cbegin(), watchedDirectories.cend());
    for (const QString& directory : current - desired)
        watcher_->removePath(directory);
    for (const QString& directory : desired - current)
        watcher_->addPath(directory);
}

void EditorArea::scheduleAutomaticReload(const Page& page) {
    pendingExternalReloads_.insert(page.document->id().value(), page.document->version());
    externalReloadTimer_->start();
}

void EditorArea::reloadPendingExternalChanges() {
    const auto pending = std::exchange(pendingExternalReloads_, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const core::DocumentId documentId(it.key());
        Page* page = pageForDocument(documentId);
        if (!page || !page->editor || page->document->isModified() ||
            page->document->version() != it.value()) {
            if (page)
                watch(*page);
            continue;
        }

        const QString filePath = page->document->filePath();
        const qint64 expectedVersion = page->document->version();
        const core::TextEncoding defaultEncoding = page->document->defaultEncoding();
        const std::optional<core::TextEncoding> preferredEncoding =
            page->document->preferredEncoding();
        auto* loadWatcher = new QFutureWatcher<editor::DocumentLoadResult>(this);
        connect(loadWatcher, &QFutureWatcher<editor::DocumentLoadResult>::finished, this,
                [this, loadWatcher, documentId, expectedVersion] {
                    const editor::DocumentLoadResult load = loadWatcher->result();
                    loadWatcher->deleteLater();
                    Page* page = pageForDocument(documentId);
                    if (!page || !page->editor) {
                        return;
                    }
                    if (page->document->isModified() ||
                        page->document->version() != expectedVersion) {
                        watch(*page);
                        return;
                    }
                    if (load.outcome != editor::DocumentLoadResult::Outcome::Loaded) {
                        if (!QFileInfo::exists(page->document->filePath())) {
                            scheduleDeletionConfirmation(documentId, page->document->filePath());
                        } else {
                            watch(*page);
                            showReloadError(*page, load.diagnostic.isEmpty()
                                                       ? tr("The file could not be reloaded.")
                                                       : load.diagnostic);
                            emit fileReloadFailed(page->document->filePath(), page->reloadError);
                        }
                        return;
                    }
                    if (!editor::DocumentSession::matchesDiskSnapshot(page->document->filePath(),
                                                                      load)) {
                        watch(*page);
                        scheduleAutomaticReload(*page);
                        return;
                    }
                    finishReload(*page, load, nullptr);
                });
        const bool autoGuessEncoding = autoGuessEncoding_;
        loadWatcher->setFuture(
            QtConcurrent::run([filePath, defaultEncoding, preferredEncoding, autoGuessEncoding] {
                return editor::DocumentSession::loadBounded(filePath, preferredEncoding,
                                                            defaultEncoding, autoGuessEncoding);
            }));
    }
}

void EditorArea::watch(Page& page) {
    const QString path = page.document->filePath();
    if (QFileInfo::exists(path) && !watcher_->files().contains(path)) {
        watcher_->addPath(path);
    }
}

} // namespace litecode::ui
