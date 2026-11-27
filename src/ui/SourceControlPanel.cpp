#include "ui/SourceControlPanel.h"
#include "ui/ConfirmationDialog.h"
#include "ui/FileIconTheme.h"
#include "ui/GitActionsMenu.h"
#include "ui/GitBranchDialog.h"
#include "ui/GitBranchOperationsDialog.h"
#include "ui/GitCloneProgressDialog.h"
#include "ui/GitHistoryWidget.h"
#include "ui/GitPreferencesMenu.h"
#include "ui/GitRepositoryPicker.h"
#include "ui/GitTagRecoveryDialog.h"
#include "ui/GitTagDialog.h"
#include "ui/GitTransferDialog.h"
#include "ui/GitWorktreeDialog.h"
#include "ui/GitStashDialog.h"
#include "ui/QuickOpenDialog.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/Controls.h"
#include "ui/components/QuickInputDialog.h"
#include "ui/components/SidebarView.h"
#include "ui/components/TreeDisclosure.h"
#include "workspace/GitService.h"
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <optional>
#include <memory>

namespace litecode::ui {
void SourceControlPanel::deleteWorktree() {
    requestGitWorktreeDeletion(this, git_);
}
namespace {
std::optional<QString> gitInput(QWidget* parent, const QString& title, const QString& label,
                                const QString& placeholder = {}) {
    components::QuickInputDialog dialog(title, label, placeholder, parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.value();
}
} // namespace
namespace {
class ElidingLabel final : public QLabel {
  public:
    using QLabel::QLabel;
    void setFullText(const QString& text) {
        fullText_ = text;
        setAccessibleName(text);
        updateText();
    }

  protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        updateText();
    }

  private:
    void updateText() { setText(fontMetrics().elidedText(fullText_, Qt::ElideRight, width())); }
    QString fullText_;
};
class ChangeDelegate final : public QStyledItemDelegate {
    static constexpr int DisclosureInset = 17;
    static constexpr int FileIconInset = DisclosureInset + 7;
    static constexpr int TrailingInset = 14;

  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static QRect actionRect(const QRect& row) {
        return {row.right() - TrailingInset - 42, row.top(), 24, row.height()};
    }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {0, 22};
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const bool dark = qApp->property("litecodeDarkTheme").toBool();
        const auto theme = Theme::tokens(dark);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        painter->save();
        painter->setClipRect(option.rect);
        painter->fillRect(option.rect, selected  ? theme.listSelectionBackground
                                       : hovered ? theme.listHoverBackground
                                                 : theme.sidebarSurface);
        if (!index.parent().isValid()) {
            auto font = option.font;
            font.setPixelSize(13);
            painter->setFont(font);
            const QFontMetrics metrics(font);
            const int center = option.rect.center().y();
            const int arrow = option.rect.left() + DisclosureInset;
            components::paintTreeDisclosure(*painter, QRect(arrow, option.rect.top(), 16, 22),
                                            option.state & QStyle::State_Open, dark);
            const QString count = QString::number(index.model()->rowCount(index));
            const int badgeWidth = metrics.horizontalAdvance(count) + 10;
            const QRect badge(option.rect.right() - TrailingInset - badgeWidth, center - 9,
                              badgeWidth, 18);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme.badgeBackground);
            painter->drawRoundedRect(badge, 9, 9);
            painter->setPen(theme.badgeForeground);
            painter->drawText(badge, Qt::AlignCenter, count);
            painter->setPen(theme.foreground);
            const int start = arrow + 19;
            painter->drawText(
                QRect(start, option.rect.top(), qMax(0, badge.left() - start - 6), 22),
                Qt::AlignVCenter,
                metrics.elidedText(index.data(Qt::UserRole).toString(), Qt::ElideRight,
                                   qMax(0, badge.left() - start - 6)));
            painter->restore();
            return;
        }
        const QString path = index.data(Qt::UserRole).toString();
        const QRect iconRect(option.rect.left() + FileIconInset, option.rect.top() + 3, 16, 16);
        fileIconForPath(path).paint(painter, iconRect);
        auto font = option.font;
        font.setPixelSize(13);
        painter->setFont(font);
        const QFontMetrics metrics(font);
        const int start = iconRect.right() + 4;
        const QRect statusRect(option.rect.right() - TrailingInset - 14, option.rect.top(), 16,
                               22);
        const QRect action = actionRect(option.rect);
        const int end = (hovered ? action.left() : statusRect.left()) - 4;
        const QString name = QFileInfo(path).fileName();
        const int available = qMax(0, end - start);
        const QString shown = metrics.elidedText(name, Qt::ElideRight, available);
        painter->setPen(selected ? theme.listSelectionForeground : theme.foreground);
        painter->drawText(QRect(start, option.rect.top(), available, 22), Qt::AlignVCenter, shown);
        const int nameWidth = metrics.horizontalAdvance(shown);
        if (shown == name && available - nameWidth > 24) {
            painter->setPen(theme.mutedForeground);
            const auto directory = QFileInfo(path).path();
            if (directory != ".")
                painter->drawText(
                    QRect(start + nameWidth + 6, option.rect.top(), available - nameWidth - 6, 22),
                    Qt::AlignVCenter,
                    metrics.elidedText(directory, Qt::ElideRight, available - nameWidth - 6));
        }
        const QString status = index.data(Qt::UserRole + 4).toString();
        painter->setPen(status == "!" ? theme.gitConflict
                        : status == "D" ? theme.gitDeleted
                        : status == "U" ? theme.gitUntracked
                        : status == "A" ? theme.gitAdded
                        : status == "R" ? theme.gitRenamed
                                        : theme.gitModified);
        painter->drawText(statusRect, Qt::AlignCenter, status);
        if (hovered) {
            if (index.data(Qt::UserRole + 1).toBool()) {
                painter->setPen(theme.foreground);
                painter->drawLine(action.center() - QPoint(5, 0), action.center() + QPoint(5, 0));
            } else
                themedIcon(":/icons/add.svg", dark)
                    .paint(painter, action.adjusted(4, 3, -4, -3));
        }
        painter->restore();
    }
};
} // namespace
SourceControlPanel::SourceControlPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("sourceControlPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    git_ = new workspace::GitService(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName("sourceControlToolbar");
    toolbar->setFixedHeight(28);
    auto* top = new QHBoxLayout(toolbar);
    top->setContentsMargins(components::SidebarView::toolbarMargins());
    top->setSpacing(0);
    summary_ = new ElidingLabel(toolbar);
    summary_->setObjectName("scmRepository");
    summary_->setTextFormat(Qt::PlainText);
    summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(summary_, 1);
    refresh_ = new components::IconButton(toolbar);
    refresh_->setToolTip(tr("Refresh"));
    refresh_->setIconSize({16, 16});
    setThemedIcon(refresh_, ":/icons/refresh.svg");
    top->addWidget(refresh_);
    auto* more = new components::IconButton(toolbar);
    more->setObjectName("scmMore");
    more->setToolTip(tr("Source Control actions"));
    more->setIconSize({16, 16});
    setThemedIcon(more, ":/icons/more.svg");
    top->addWidget(more);
    layout->addWidget(toolbar);
    auto* pages = new QStackedWidget(this);
    pages->setObjectName("scmViews");
    auto* viewActions = new QActionGroup(this);
    auto* changesAction = new QAction(tr("Changes"), viewActions);
    changesAction->setObjectName("scmChangesView");
    changesAction->setCheckable(true);
    changesAction->setChecked(true);
    auto* graphAction = new QAction(tr("Graph"), viewActions);
    graphAction->setObjectName("scmHistoryView");
    graphAction->setCheckable(true);
    auto* changesPage = new components::SidebarView(pages);
    changesPage->setObjectName("scmChangesPage");
    auto* content = changesPage->contentLayout();
    pages->addWidget(changesPage);
    history_ = new GitHistoryWidget(pages);
    auto* navigation = new QWidget(toolbar);
    navigation->setObjectName("scmGraphNavigation");
    auto* actions = new QHBoxLayout(navigation);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(0);
    auto* filter = new components::IconButton(navigation);
    filter->setText(tr("Auto"));
    filter->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    filter->setMinimumWidth(60);
    filter->setIconSize({16, 16});
    setThemedIcon(filter, ":/icons/git-branch.svg");
    filter->setObjectName("scmGraphFilter");
    filter->setToolTip(tr("Filter graph references"));
    actions->addWidget(filter);
    connect(filter, &QToolButton::clicked, history_,
            [this, filter] { history_->chooseFilter(filter); });
    connect(history_, &GitHistoryWidget::filterChanged, filter,
            [this, filter] { filter->setText(history_->filterLabel()); });
    const auto graphActionButton = [&](const QString& name, const QString& icon,
                                       const QString& tooltip,
                                       const std::function<void()>& callback) {
        auto* button = new components::IconButton(navigation);
        button->setObjectName(name);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setIconSize({16, 16});
        setThemedIcon(button, icon);
        actions->addWidget(button);
        connect(button, &QToolButton::clicked, this, callback);
    };
    graphActionButton("scmGraphCurrent", ":/icons/target.svg", tr("Go to Current Commit"),
                      [this] { history_->revealCurrentCommit(); });
    graphActionButton("scmGraphFetch", ":/icons/repo-fetch.svg", tr("Fetch"),
                      [this] { git_->fetch(); });
    graphActionButton("scmGraphPull", ":/icons/arrow-down.svg", tr("Pull"), [this] {
        if (!git_->busy() && allowWorktreeChange())
            git_->pull();
    });
    graphActionButton("scmGraphPush", ":/icons/arrow-up.svg", tr("Push"), [this] { git_->push(); });
    top->insertWidget(1, navigation);
    navigation->hide();
    connect(pages, &QStackedWidget::currentChanged, navigation,
            [navigation](int index) { navigation->setVisible(index == 1); });
    connect(git_, &workspace::GitService::busyChanged, navigation, [this, navigation](bool busy) {
        for (const auto& name : {"scmGraphFetch", "scmGraphPull", "scmGraphPush"})
            navigation->findChild<QToolButton*>(name)->setEnabled(!busy &&
                                                                  !git_->repository().isEmpty());
    });
    pages->addWidget(history_);
    layout->addWidget(pages, 1);
    connect(changesAction, &QAction::triggered, this, [pages] { pages->setCurrentIndex(0); });
    connect(graphAction, &QAction::triggered, this, [pages] { pages->setCurrentIndex(1); });
    connect(pages, &QStackedWidget::currentChanged, this, [this](int index) {
        render();
        emit graphViewChanged(index == 1);
    });
    connect(history_, &GitHistoryWidget::comparisonRequested, this,
            &SourceControlPanel::comparisonRequested);
    auto* inputArea = new QWidget(this);
    inputArea->setObjectName("scmInputArea");
    auto* inputLayout = new QVBoxLayout(inputArea);
    inputLayout->setContentsMargins(components::SidebarView::controlMargins());
    inputLayout->setSpacing(4);
    message_ = new QPlainTextEdit(inputArea);
    message_->setObjectName("gitCommitMessage");
    message_->setPlaceholderText(tr("Message (Ctrl+Enter to commit)"));
    message_->setFixedHeight(62);
    inputLayout->addWidget(message_);
    commit_ = new components::Button(tr("Commit"), inputArea, components::ButtonVariant::Primary);
    commit_->setToolTip(tr("Commit staged changes"));
    commit_->setObjectName("gitCommitButton");
    static_cast<components::Button*>(commit_)->setTextCentered(true);
    setThemedIcon(commit_, ":/icons/check.svg");
    commit_->setIconSize({16, 16});
    inputLayout->addWidget(commit_);
    content->addWidget(inputArea);
    notice_ = new QLabel(this);
    notice_->setObjectName("scmNotice");
    notice_->setContentsMargins(components::SidebarView::controlMargins());
    notice_->setTextFormat(Qt::PlainText);
    notice_->setWordWrap(true);
    notice_->hide();
    content->addWidget(notice_);
    emptyWelcome_ = new QWidget(changesPage);
    emptyWelcome_->setObjectName("scmEmptyWelcome");
    emptyWelcome_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* emptyLayout = new QVBoxLayout(emptyWelcome_);
    emptyLayout->setContentsMargins(20, 14, 20, 0);
    emptyLayout->setSpacing(12);
    empty_ = new QLabel(emptyWelcome_);
    empty_->setWordWrap(true);
    empty_->setObjectName("scmChangesEmpty");
    empty_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    emptyLayout->addWidget(empty_);
    initialize_ = new components::Button(tr("Initialize Repository"), emptyWelcome_,
                                         components::ButtonVariant::Primary);
    initialize_->setObjectName("gitInitializeRepository");
    initialize_->setMinimumHeight(32);
    emptyLayout->addWidget(initialize_);
    emptyDocs_ = new QLabel(emptyWelcome_);
    emptyDocs_->setObjectName("scmEmptyDocs");
    emptyDocs_->setWordWrap(true);
    emptyDocs_->setTextFormat(Qt::RichText);
    emptyDocs_->setOpenExternalLinks(true);
    emptyDocs_->setText(tr("To learn more about Git and source control, "
                           "<a href=\"https://code.visualstudio.com/docs/sourcecontrol/overview\">"
                           "read VS Code's docs</a>."));
    emptyLayout->addWidget(emptyDocs_);
    content->addWidget(emptyWelcome_, 0, Qt::AlignTop);
    connect(initialize_, &QPushButton::clicked, git_, &workspace::GitService::initializeRepository);
    changes_ = new QTreeWidget(this);
    changes_->setRootIsDecorated(false);
    changes_->setObjectName("gitChanges");
    changes_->setHeaderHidden(true);
    changes_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    changes_->setTextElideMode(Qt::ElideRight);
    changes_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    changes_->setMouseTracking(true);
    changes_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    changes_->setIndentation(0);
    changes_->setUniformRowHeights(true);
    changes_->setItemDelegate(new ChangeDelegate(changes_));
    changes_->viewport()->installEventFilter(this);
    TransientScrollBars::install(changes_);
    content->addWidget(changes_, 1);
    connect(refresh_, &QToolButton::clicked, git_, &workspace::GitService::refresh);
    connect(refresh_, &QToolButton::clicked, history_, &GitHistoryWidget::reload);
    connect(git_, &workspace::GitService::operationFinished, history_, &GitHistoryWidget::reload);
    connect(git_, &workspace::GitService::repositoryCloned, this,
            &SourceControlPanel::repositoryCloned);
    connect(git_, &workspace::GitService::worktreeCreated, this,
            &SourceControlPanel::worktreeCreated);
    connect(git_, &workspace::GitService::cloneCancelled, this,
            [this](const QString& destination) {
                ConfirmationDialog::showMessage(
                    this, tr("Clone Canceled"),
                    tr("The clone was canceled. An incomplete destination may remain at %1. Inspect it before retrying.")
                        .arg(destination));
            });
    connect(git_, &workspace::GitService::outputLine, this,
            &SourceControlPanel::gitOutputLine);
    connect(more, &QToolButton::clicked, this, [this, more, changesAction, graphAction] {
        components::Menu menu(this);
        menu.setObjectName("scmActionsMenu");
        menu.setMinimumWidth(280);
        components::Menu views(tr("Views"), &menu);
        views.setMinimumWidth(200);
        views.addAction(changesAction);
        views.addAction(graphAction);
        menu.addMenu(&views);
        auto* repositories = menu.addAction(tr("Repositories…"), this, [this] {
            const auto generation = workspaceGeneration_;
            requestGitRepository(this, workspaceRoot_, git_->repository(),
                                 [this, generation](const QString& repository) {
                                     if (generation == workspaceGeneration_)
                                         selectRepository(repository);
                                 });
        });
        repositories->setEnabled(!git_->busy());
        menu.addSeparator();
        auto* recovery = menu.addAction(tr("Restore Checkout Recovery Copy…"), this, [this] {
            if (!git_->busy()) {
                restoringRecovery_ = true;
                git_->listRecoveryCopies();
            }
        });
        recovery->setEnabled(!git_->busy() && !git_->repository().isEmpty());
        auto* tagRecovery = menu.addAction(tr("Replace Conflicting Tag…"), this,
                                           [this] { requestGitTagRecovery(this, git_); });
        auto* tagRestore = menu.addAction(tr("Restore Tag Backup…"), this,
                                          [this] { requestGitTagRecovery(this, git_, true); });
        tagRecovery->setEnabled(recovery->isEnabled());
        tagRestore->setEnabled(recovery->isEnabled());
        auto tagAction = [this, &menu](const QString& label, GitTagAction action) {
            auto* item = menu.addAction(label, this,
                                        [this, action] { requestGitTagAction(this, git_, action); });
            item->setEnabled(!git_->busy() && !git_->repository().isEmpty());
            return item;
        };
        auto* createTag = tagAction(tr("Create Tag…"), GitTagAction::Create);
        auto* deleteTag = tagAction(tr("Delete Tag…"), GitTagAction::DeleteLocal);
        auto* deleteRemoteTag = tagAction(tr("Delete Remote Tag…"), GitTagAction::DeleteRemote);
        auto* pushTags = tagAction(tr("Push Tags"), GitTagAction::Push);
        QFile gitPointer(QDir(git_->repository()).filePath(".git"));
        const bool linkedWorktree = gitPointer.open(QIODevice::ReadOnly) &&
            QString::fromUtf8(gitPointer.read(4096)).replace('\\', '/').contains("/worktrees/");
        QAction* createWorktree = nullptr;
        QAction* openWorktree = nullptr;
        QAction* openWorktreeNewWindow = nullptr;
        QAction* deleteLinkedWorktree = nullptr;
        if (linkedWorktree) {
            openWorktree = menu.addAction(tr("Open Worktree"), this, [this] {
                emit openWorktreeRequested(git_->repository(), false);
            });
            openWorktreeNewWindow = menu.addAction(tr("Open Worktree in New Window"), this,
                                                   [this] {
                emit openWorktreeRequested(git_->repository(), true);
            });
            deleteLinkedWorktree = menu.addAction(tr("Delete Worktree..."), this, [this] {
                const QString active = git_->repository();
                const quint64 generation = git_->workspaceGeneration();
                auto connection = std::make_shared<QMetaObject::Connection>();
                *connection = connect(git_, &workspace::GitService::worktreesReady, this,
                    [this, active, generation, connection](const QStringList& paths) {
                        disconnect(*connection);
                        if (git_->workspaceGeneration() != generation ||
                            git_->repository() != active)
                            return;
                        if (paths.size() < 2 || paths.first() == active) {
                            ConfirmationDialog::showMessage(
                                this, tr("Delete Worktree"),
                                tr("Open the main repository before deleting this worktree."));
                            return;
                        }
                        emit linkedWorktreeDeletionRequested(paths.first(), active);
                    });
                git_->listWorktrees();
            });
            for (auto* item : {openWorktree, openWorktreeNewWindow, deleteLinkedWorktree})
                item->setEnabled(!git_->busy());
        } else {
            createWorktree = menu.addAction(tr("Create Worktree..."), this,
                                            [this] { requestGitWorktree(this, git_); });
            createWorktree->setEnabled(!git_->busy() && !git_->repository().isEmpty());
        }
        auto* stage = menu.addAction(tr("Stage All Changes"), this, [this] {
            changes_->clearSelection();
            for (int i = 0; i < changes_->topLevelItemCount(); ++i)
                for (int j = 0; j < changes_->topLevelItem(i)->childCount(); ++j)
                    changes_->topLevelItem(i)->child(j)->setSelected(true);
            transfer(true);
        });
        auto* unstage = menu.addAction(tr("Unstage All Changes"), this, [this] {
            changes_->clearSelection();
            for (int i = 0; i < changes_->topLevelItemCount(); ++i)
                for (int j = 0; j < changes_->topLevelItem(i)->childCount(); ++j)
                    changes_->topLevelItem(i)->child(j)->setSelected(true);
            transfer(false);
        });
        auto* discardAll = menu.addAction(tr("Discard All Changes"), this, [this] {
            if (git_->busy() || git_->repository().isEmpty() || !allowWorktreeChange())
                return;
            int tracked = 0, untracked = 0;
            for (const auto& change : git_->status().changes) {
                if (change.conflict)
                    continue;
                if (change.index == '?')
                    ++untracked;
                else if (change.worktree != ' ')
                    ++tracked;
            }
            if (!tracked && !untracked)
                return;
            const QString message = tracked && untracked
                ? tr("Discard working-tree changes in %1 tracked file(s) and move %2 untracked file(s) to Trash? Staged changes are preserved. Tracked edits cannot be recovered.")
                      .arg(tracked).arg(untracked)
                : tracked
                    ? tr("Discard working-tree changes in %1 tracked file(s)? Staged changes are preserved. Tracked edits cannot be recovered.")
                          .arg(tracked)
                    : tr("Move %1 untracked file(s) to Trash? Staged changes are preserved.")
                          .arg(untracked);
            const auto choice = ConfirmationDialog::ask(
                this, tr("Discard All Changes"), message,
                tracked ? tr("Discard All Changes") : tr("Move to Trash"),
                tracked && untracked ? tr("Discard Tracked Only") : QString{}, tr("Cancel"));
            if (choice == ConfirmationChoice::Primary)
                git_->discardWorkingChanges(true);
            else if (choice == ConfirmationChoice::Secondary && tracked && untracked)
                git_->discardWorkingChanges(false);
        });
        stage->setEnabled(!git_->busy() && !git_->repository().isEmpty());
        unstage->setEnabled(stage->isEnabled());
        discardAll->setEnabled(stage->isEnabled());
        menu.addSeparator();
        auto* branches =
            menu.addAction(tr("Checkout Branch…"), this, &SourceControlPanel::chooseBranch);
        auto* create = menu.addAction(tr("Create Branch…"), this, [this] { createBranch(); });
        auto* createFrom =
            menu.addAction(tr("Create Branch From…"), this, [this] { createBranch(true); });
        auto branchAction = [this, &menu](const QString& label, GitBranchAction action) {
            auto* item = menu.addAction(label, this, [this, action] {
                if ((action != GitBranchAction::Merge && action != GitBranchAction::Rebase) ||
                    allowWorktreeChange())
                    requestGitBranchAction(this, git_, action);
            });
            item->setEnabled(!git_->busy() && !git_->repository().isEmpty());
            return item;
        };
        auto* merge = branchAction(tr("Merge…"), GitBranchAction::Merge);
        auto* rebase = branchAction(tr("Rebase Branch…"), GitBranchAction::Rebase);
        auto* renameBranch = branchAction(tr("Rename Branch…"), GitBranchAction::Rename);
        auto* deleteBranch = branchAction(tr("Delete Branch…"), GitBranchAction::DeleteLocal);
        auto* deleteRemoteBranch =
            branchAction(tr("Delete Remote Branch…"), GitBranchAction::DeleteRemote);
        auto stashAction = [this, &menu](const QString& label, GitStashAction action) {
            auto* item = menu.addAction(label, this, [this, action] {
                if (action == GitStashAction::Apply || action == GitStashAction::ApplyLatest ||
                    action == GitStashAction::Pop || action == GitStashAction::PopLatest) {
                    if (!allowWorktreeChange())
                        return;
                }
                requestGitStashAction(this, git_, action);
            });
            item->setEnabled(!git_->busy() && !git_->repository().isEmpty());
            return item;
        };
        auto* stash = stashAction(tr("Stash"), GitStashAction::Create);
        auto* stashUntracked = stashAction(tr("Stash (Include Untracked)"), GitStashAction::CreateUntracked);
        auto* stashStaged = stashAction(tr("Stash Staged"), GitStashAction::CreateStaged);
        auto* applyLatest = stashAction(tr("Apply Latest Stash"), GitStashAction::ApplyLatest);
        auto* apply = stashAction(tr("Apply Stash…"), GitStashAction::Apply);
        auto* popLatest = stashAction(tr("Pop Latest Stash"), GitStashAction::PopLatest);
        auto* pop = stashAction(tr("Pop Stash…"), GitStashAction::Pop);
        auto* drop = stashAction(tr("Drop Stash…"), GitStashAction::Drop);
        auto* dropAll = stashAction(tr("Drop All Stashes…"), GitStashAction::DropAll);
        auto* viewStash = stashAction(tr("View Stash…"), GitStashAction::View);
        menu.addSeparator();
        auto* fetch = menu.addAction(tr("Fetch"), this, [this] { git_->fetch(); });
        auto* fetchAll =
            menu.addAction(tr("Fetch All Remotes"), this, [this] { git_->fetch({}, true); });
        auto* fetchPrune =
            menu.addAction(tr("Fetch (Prune)"), this, [this] { git_->fetch({}, false, true); });
        auto* pull = menu.addAction(tr("Pull"), this, [this] {
            if (allowWorktreeChange())
                git_->pull();
        });
        auto* pullRebase = menu.addAction(tr("Pull (Rebase)"), this, [this] {
            if (allowWorktreeChange())
                git_->pullRebase();
        });
        auto* pullFrom = menu.addAction(tr("Pull From..."), this, [this] {
            if (allowWorktreeChange())
                requestGitTransfer(this, git_, GitTransferAction::PullFrom);
        });
        auto* push = menu.addAction(tr("Push"), this, [this] { git_->push(); });
        auto* pushTo = menu.addAction(tr("Push To..."), this, [this] {
            requestGitTransfer(this, git_, GitTransferAction::PushTo);
        });
        auto* clone = menu.addAction(tr("Clone..."), this, [this] {
            const auto url = gitInput(this, tr("Clone Repository"), tr("Repository URL"),
                                      tr("Enter repository URL or path"));
            if (!url)
                return;
            const QString parent = QFileDialog::getExistingDirectory(
                this, tr("Select as Repository Destination"),
                workspaceRoot_.isEmpty() ? QDir::homePath() : workspaceRoot_);
            if (!parent.isEmpty()) {
                git_->cloneRepository(*url, parent);
                if (git_->busy()) {
                    GitCloneProgressDialog progress(git_, this);
                    progress.exec();
                }
            }
        });
        clone->setEnabled(!git_->busy());
        const auto confirmForcePush = [this] {
            return !preferences_.confirmForcePush ||
                   ConfirmationDialog::ask(
                       this, tr("Force Push"),
                       tr("Force pushing can overwrite commits on the remote branch. Continue?"),
                       tr("Force Push"), {}, tr("Cancel")) == ConfirmationChoice::Primary;
        };
        QAction* pushForce = nullptr;
        QAction* pushToForce = nullptr;
        if (preferences_.allowForcePush) {
            pushForce = menu.addAction(tr("Push (Force)"), this, [this, confirmForcePush] {
                if (confirmForcePush())
                    git_->pushForce();
            });
            pushToForce = menu.addAction(tr("Push to... (Force)"), this,
                                         [this] {
                requestGitTransfer(this, git_, GitTransferAction::PushToForce,
                                   preferences_.confirmForcePush);
            });
        }
        const auto syncAction = [this, &menu](const QString& label, bool rebase) {
            return menu.addAction(label, this, [this, rebase] {
                if (!allowWorktreeChange())
                    return;
                if (ConfirmationDialog::ask(
                        this, tr("Synchronize Changes"),
                        tr("Pull commits from the upstream branch, then push local commits only if the pull succeeds?"),
                        tr("Synchronize"), {}, tr("Cancel")) == ConfirmationChoice::Primary)
                    git_->sync(rebase);
            });
        };
        auto* sync = syncAction(tr("Sync"), false);
        auto* syncRebase = syncAction(tr("Sync (Rebase)"), true);
        auto* publish = menu.addAction(tr("Publish Branch…"), this, [this] { chooseRemote(true); });
        menu.addSeparator();
        auto* addRemote = menu.addAction(tr("Add Remote…"), this, [this] {
            const auto url =
                gitInput(this, tr("Add Remote"), tr("Remote URL or local repository path"));
            if (!url)
                return;
            const auto name = gitInput(this, tr("Add Remote"), tr("Remote name"), "origin");
            if (name)
                git_->addRemote(*name, *url);
        });
        auto* removeRemote =
            menu.addAction(tr("Remove Remote…"), this, [this] { chooseRemote(false); });
        for (auto* action : {branches, create, createFrom, fetch, fetchAll, fetchPrune, pull,
                             pullRebase, pullFrom, push, pushTo, sync, syncRebase, publish,
                             addRemote, removeRemote})
            action->setEnabled(stage->isEnabled());
        if (pushForce)
            pushForce->setEnabled(stage->isEnabled());
        if (pushToForce)
            pushToForce->setEnabled(stage->isEnabled());
        auto makeCommitAction = [this, &menu](const QString& label, workspace::GitCommitOptions options) {
            // VS Code omits no-verify variants entirely until the setting is enabled.
            if (options.noVerify && !preferences_.allowNoVerifyCommit)
                return static_cast<QAction*>(nullptr);
            auto* item = menu.addAction(label, this, [this, options] {
                if (options.scope == workspace::GitCommitScope::All && !allowWorktreeChange())
                    return;
                auto message = message_->toPlainText();
                if (message.trimmed().isEmpty() && !options.amend) {
                    const auto entered = gitInput(this, tr("Commit"), tr("Commit message"));
                    if (!entered)
                        return;
                    message = *entered;
                }
                if (options.noVerify && ConfirmationDialog::ask(
                        this, tr("Commit Without Verification"),
                        tr("Skip pre-commit hooks for this commit?"), tr("Commit"), {}, tr("Cancel")) !=
                                            ConfirmationChoice::Primary)
                    return;
                git_->commitWithOptions(message, options);
            });
            item->setEnabled(!git_->busy() && !git_->repository().isEmpty());
            return item;
        };
        using Scope = workspace::GitCommitScope;
        auto* commitAction = makeCommitAction(tr("Commit"), {Scope::Staged});
        auto* commitStaged = makeCommitAction(tr("Commit Staged"), {Scope::Staged});
        auto* commitAll = makeCommitAction(tr("Commit All"), {Scope::All});
        auto* undoCommit = menu.addAction(tr("Undo Commit"), this, [this] {
            if (ConfirmationDialog::ask(this, tr("Undo Commit"),
                                        tr("Undo the last local commit and keep its file changes?"),
                                        tr("Undo Commit"), {}, tr("Cancel")) ==
                ConfirmationChoice::Primary)
                git_->undoCommit();
        });
        undoCommit->setEnabled(!git_->busy() && !git_->repository().isEmpty());
        auto* abortRebase = menu.addAction(tr("Abort Rebase"), this,
                                           [this] { git_->abortRebase(); });
        abortRebase->setEnabled(undoCommit->isEnabled());
        auto* commitNoVerify = makeCommitAction(tr("Commit (No Verify)"), {Scope::Staged, false, false, true});
        auto* stagedNoVerify = makeCommitAction(tr("Commit Staged (No Verify)"), {Scope::Staged, false, false, true});
        auto* allNoVerify = makeCommitAction(tr("Commit All (No Verify)"), {Scope::All, false, false, true});
        auto* amend = makeCommitAction(tr("Commit (Amend)"), {Scope::Staged, true});
        auto* stagedAmend = makeCommitAction(tr("Commit Staged (Amend)"), {Scope::Staged, true});
        auto* allAmend = makeCommitAction(tr("Commit All (Amend)"), {Scope::All, true});
        auto* amendNoVerify = makeCommitAction(tr("Commit (Amend, No Verify)"), {Scope::Staged, true, false, true});
        auto* stagedAmendNoVerify = makeCommitAction(tr("Commit Staged (Amend, No Verify)"), {Scope::Staged, true, false, true});
        auto* allAmendNoVerify = makeCommitAction(tr("Commit All (Amend, No Verify)"), {Scope::All, true, false, true});
        auto* signedCommit = makeCommitAction(tr("Commit (Signed Off)"), {Scope::Staged, false, true});
        auto* signedStaged = makeCommitAction(tr("Commit Staged (Signed Off)"), {Scope::Staged, false, true});
        auto* signedAll = makeCommitAction(tr("Commit All (Signed Off)"), {Scope::All, false, true});
        auto* signedNoVerify = makeCommitAction(tr("Commit (Signed Off, No Verify)"), {Scope::Staged, false, true, true});
        auto* signedStagedNoVerify = makeCommitAction(tr("Commit Staged (Signed Off, No Verify)"), {Scope::Staged, false, true, true});
        auto* signedAllNoVerify = makeCommitAction(tr("Commit All (Signed Off, No Verify)"), {Scope::All, false, true, true});
        QList<QAction*> pullPushActions{sync, syncRebase, nullptr, pull, pullRebase,
                                        pullFrom, push};
        if (pushForce)
            pullPushActions.append(pushForce);
        pullPushActions.append(pushTo);
        if (pushToForce)
            pullPushActions.append(pushToForce);
        pullPushActions.append({nullptr, fetch, fetchPrune, fetchAll});
        groupGitActionsMenu(menu, views.menuAction(), repositories,
                            {pull, push, clone, branches, fetch},
                            {{tr("Commit"), {commitAction, commitStaged, commitAll, undoCommit,
                                              abortRebase, nullptr, commitNoVerify, stagedNoVerify,
                                              allNoVerify, nullptr, amend, stagedAmend, allAmend,
                                              nullptr, amendNoVerify, stagedAmendNoVerify,
                                              allAmendNoVerify, nullptr, signedCommit, signedStaged,
                                              signedAll, nullptr, signedNoVerify,
                                              signedStagedNoVerify, signedAllNoVerify}},
                             {tr("Changes"), {stage, unstage, nullptr, discardAll}},
                             {tr("Pull, Push"), pullPushActions},
                             {tr("Branch"), {merge, rebase, nullptr, create, createFrom, nullptr,
                                              renameBranch, deleteBranch, deleteRemoteBranch,
                                              nullptr, publish}},
                             {tr("Remote"), {addRemote, removeRemote}},
                             {tr("Stash"), {stash, stashUntracked,
                                             git_->supportsStagedStash() ? stashStaged : nullptr, nullptr,
                                             applyLatest, apply, nullptr, popLatest, pop,
                                             nullptr, drop, dropAll, nullptr, viewStash}},
                             {tr("Tags"), {createTag, deleteTag, deleteRemoteTag, nullptr,
                                            pushTags}},
                             {tr("Worktrees"), {openWorktree, openWorktreeNewWindow,
                                                 createWorktree, deleteLinkedWorktree}},
                             {tr("Recovery"), {recovery, tagRecovery, tagRestore}}});
        menu.addSeparator();
        menu.addAction(tr("Show Git Output"), this,
                       [this] { emit gitOutputRequested(); });
        menu.addSeparator();
        addGitPreferencesMenu(
            menu, preferences_,
            [this] {
                git_->setPreferences(preferences_);
                emit gitPreferencesChanged();
            },
            this);
        menu.exec(more->mapToGlobal(QPoint(0, more->height())));
    });
    changes_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(changes_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto* item = changes_->itemAt(point);
        if (!item || !item->parent())
            return;
        if (!item->isSelected()) {
            changes_->clearSelection();
            item->setSelected(true);
        }
        changes_->setCurrentItem(item);
        components::Menu menu(this);
        const bool staged = item->data(0, Qt::UserRole + 1).toBool();
        const auto path = item->data(0, Qt::UserRole).toString();
        auto* transferAction = menu.addAction(staged ? tr("Unstage Changes") : tr("Stage Changes"),
                                              this, [this, staged] { transfer(!staged); });
        transferAction->setEnabled(!git_->busy());
        menu.addAction(tr("Open Changes"), this, &SourceControlPanel::showDiff);
        menu.addAction(tr("Open File"), this, [this, path] {
            emit openFileRequested(QDir(git_->repository()).filePath(path));
        });
        menu.exec(changes_->viewport()->mapToGlobal(point));
    });
    connect(commit_, &QPushButton::clicked, this,
            [this] { git_->commit(message_->toPlainText()); });
    auto* commitShortcut = new QShortcut(QKeySequence("Ctrl+Return"), message_);
    commitShortcut->setContext(Qt::WidgetShortcut);
    connect(commitShortcut, &QShortcut::activated, commit_, &QPushButton::click);
    connect(message_, &QPlainTextEdit::textChanged, this, &SourceControlPanel::updateActions);
    connect(git_, &workspace::GitService::commitSucceeded, this, [this] {
        message_->clear();
        notice_->hide();
        history_->reload();
    });
    connect(git_, &workspace::GitService::commitUndone, this,
            [this](const QString& message) { message_->setPlainText(message); });
    connect(git_, &workspace::GitService::publishRequired, this, [this] {
        if (ConfirmationDialog::ask(this, tr("Publish Branch"),
                                    tr("This branch has no upstream. Publish it to a remote?"),
                                    tr("Publish Branch"), {},
                                    tr("Cancel")) == ConfirmationChoice::Primary)
            chooseRemote(true);
    });
    connect(changes_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        if (!item->parent())
            item->setExpanded(!item->isExpanded());
        else
            showDiff();
    });
    connect(git_, &workspace::GitService::changed, this, &SourceControlPanel::render);
    connect(git_, &workspace::GitService::busyChanged, this, [this] { updateActions(); });
    connect(git_, &workspace::GitService::error, this, [this](const QString& error) {
        choosingBranch_ = choosingRemote_ = false;
        if (error.startsWith(tr("Git installation not found")))
            empty_->setText(tr("Install Git to use Source Control. No GitHub sign-in or SSH key is needed for local changes and commits."));
        notice_->setText(error);
        notice_->show();
    });
    connect(git_, &workspace::GitService::comparisonReady, this,
            &SourceControlPanel::comparisonRequested);
    connect(
        git_, &workspace::GitService::checkoutFailed, this,
        [this](const QString& repository, const QString& ref, bool detached, const QString&) {
            if (repository == git_->repository())
                recoverCheckout(ref, detached);
        },
        Qt::QueuedConnection);
    connect(git_, &workspace::GitService::recoveryCopyCreated, this, [this](const QString& ref) {
        notice_->setText(
            tr("Original changes are retained at %1 and in Git's stash list.").arg(ref));
        notice_->show();
    });
    connect(git_, &workspace::GitService::tagBackupCreated, this, [this](const QString& ref) {
        notice_->setText(tr("Previous tag object retained at %1.").arg(ref));
        notice_->show();
    });
    connect(
        git_, &workspace::GitService::tagConflict, this,
        [this](const QString& repository, const QString&) {
            if (repository == git_->repository())
                requestGitTagRecovery(this, git_);
        },
        Qt::QueuedConnection);
    connect(git_, &workspace::GitService::recoveryCopiesReady, this,
            [this](const QStringList& refs) {
                if (!restoringRecovery_)
                    return;
                restoringRecovery_ = false;
                if (refs.isEmpty()) {
                    ConfirmationDialog::showMessage(
                        this, tr("Checkout Recovery"),
                        tr("No LiteCode checkout recovery copies in this repository."));
                    return;
                }
                QVector<QuickPickItem> items;
                for (const auto& ref : refs)
                    items.append({ref, ref.section('/', -1), tr("Retained recovery copy")});
                QuickOpenDialog picker(tr("Checkout Recovery"), tr("Select a recovery copy"), items,
                                       {}, this);
                const auto repository = git_->repository();
                const auto generation = workspaceGeneration_;
                if (picker.exec() == QDialog::Accepted && repository == git_->repository() &&
                    generation == workspaceGeneration_ && allowWorktreeChange() &&
                    ConfirmationDialog::ask(
                        this, tr("Restore Recovery Copy"),
                        tr("Reapply this copy to the current branch, including its staged changes? "
                           "Git may report conflicts. The recovery copy will be retained."),
                        tr("Restore"), {}, tr("Cancel")) == ConfirmationChoice::Primary &&
                    generation == workspaceGeneration_ && repository == git_->repository())
                    git_->restoreRecoveryCopy(picker.selectedItemId());
            });
    connect(git_, &workspace::GitService::branchesReady, this,
            [this](const QVector<workspace::GitBranch>& branches) {
                if (!choosingBranch_)
                    return;
                choosingBranch_ = false;
                if (creatingFrom_) {
                    creatingFrom_ = false;
                    QVector<QuickPickItem> revisions{{"HEAD", tr("Current branch (HEAD)"), {}}};
                    for (const auto& branch : branches)
                        revisions.append({branch.ref, branch.name, branch.commit.left(7)});
                    QuickOpenDialog picker(tr("Create Branch From"),
                                           tr("Select a starting revision"), revisions, "HEAD",
                                           this);
                    const auto repository = git_->repository();
                    const auto generation = workspaceGeneration_;
                    if (picker.exec() != QDialog::Accepted || repository != git_->repository() ||
                        generation != workspaceGeneration_)
                        return;
                    const auto start = picker.selectedItemId();
                    if (const auto name = requestGitBranchName(this, repository, preferences_))
                        if (repository == git_->repository() &&
                            generation == workspaceGeneration_ && allowWorktreeChange())
                            git_->createBranch(*name, start);
                    return;
                }
                QVector<QuickPickItem> items;
                items.append({":create", tr("Create new branch…"), {}});
                items.append({":createFrom", tr("Create new branch from…"), {}});
                items.append({":detached", tr("Checkout detached…"), {}});
                QString initial;
                for (const auto& branch : branches) {
                    if (branch.current)
                        initial = branch.ref;
                    items.append({branch.ref, branch.name,
                                  branch.current  ? tr("Current branch")
                                  : branch.tag    ? tr("Tag")
                                  : branch.remote ? tr("Remote branch")
                                                  : branch.upstream});
                }
                QuickOpenDialog picker(tr("Checkout Branch"), tr("Select a branch"), items, initial,
                                       this);
                const auto repository = git_->repository();
                const auto generation = workspaceGeneration_;
                if (picker.exec() != QDialog::Accepted)
                    return;
                if (repository != git_->repository() || generation != workspaceGeneration_)
                    return;
                const auto selected = picker.selectedItemId();
                if (selected == ":create") {
                    createBranch();
                } else if (selected == ":createFrom") {
                    createBranch(true);
                } else if (selected == ":detached") {
                    QVector<QuickPickItem> revisions;
                    for (const auto& branch : branches)
                        revisions.append({branch.ref, branch.name, branch.commit.left(7)});
                    QuickOpenDialog detached(tr("Checkout Detached"), tr("Select a revision"),
                                             revisions, {}, this);
                    if (detached.exec() == QDialog::Accepted && repository == git_->repository() &&
                        generation == workspaceGeneration_ && allowWorktreeChange())
                        git_->checkoutDetached(detached.selectedItemId());
                } else if (!selected.isEmpty() && allowWorktreeChange())
                    git_->checkoutBranch(selected);
            });
    connect(git_, &workspace::GitService::remotesReady, this,
            [this](const QVector<workspace::GitRemote>& remotes) {
                if (!choosingRemote_)
                    return;
                choosingRemote_ = false;
                if (remotes.isEmpty()) {
                    ConfirmationDialog::showMessage(
                        this, tr("Git Remotes"),
                        tr("No remotes configured. Use Add Remote first."));
                    return;
                }
                QVector<QuickPickItem> items;
                for (const auto& remote : remotes)
                    items.append({remote.name, remote.name, remote.fetchUrl});
                QuickOpenDialog picker(publishing_ ? tr("Publish Branch") : tr("Remove Remote"),
                                       tr("Select a remote"), items, {}, this);
                const auto repository = git_->repository();
                const auto generation = workspaceGeneration_;
                if (picker.exec() != QDialog::Accepted || picker.selectedItemId().isEmpty())
                    return;
                if (repository != git_->repository() || generation != workspaceGeneration_)
                    return;
                if (publishing_)
                    git_->push(picker.selectedItemId(), true);
                else if (ConfirmationDialog::ask(
                             this, tr("Remove Remote"),
                             tr("Remove remote '%1'? This removes its local configuration and "
                                "remote-tracking references, not the remote repository.")
                                 .arg(picker.selectedItemId()),
                             tr("Remove"), {}, tr("Cancel")) == ConfirmationChoice::Primary &&
                         generation == workspaceGeneration_ && repository == git_->repository())
                    git_->removeRemote(picker.selectedItemId());
            });
    auto* poll = new QTimer(this);
    poll->setInterval(5000);
    connect(poll, &QTimer::timeout, this, [this] {
        // Explorer shares this status even while Source Control is not the active view.
        if (!workspaceRoot_.isEmpty() && qApp->applicationState() == Qt::ApplicationActive &&
            !QApplication::activeModalWidget())
            git_->refresh();
    });
    poll->start();
    render();
}
void SourceControlPanel::setWorkspace(const QString& path) {
    if (!git_->repository().isEmpty())
        commitMessages_[git_->repository()] = message_->toPlainText();
    workspaceRoot_ = path;
    ++workspaceGeneration_;
    restoringRecovery_ = false;
    creatingFrom_ = false;
    choosingBranch_ = choosingRemote_ = false;
    message_->setPlainText(commitMessages_.value(QFileInfo(path).canonicalFilePath()));
    notice_->hide();
    git_->setWorkspace(path);
    history_->setWorkspace(path);
}
void SourceControlPanel::selectRepository(const QString& path) {
    if (git_->busy()) {
        ConfirmationDialog::showMessage(
            this, tr("Git Operation in Progress"),
            tr("Wait for the current Git operation before switching repositories."));
        return;
    }
    const auto root = workspaceRoot_;
    if (!git_->repository().isEmpty())
        commitMessages_[git_->repository()] = message_->toPlainText();
    setWorkspace(path);
    workspaceRoot_ = root;
    message_->setPlainText(commitMessages_.value(QFileInfo(path).canonicalFilePath()));
}
void SourceControlPanel::recoverCheckout(const QString& ref, bool detached) {
    const auto repository = git_->repository();
    const auto generation = workspaceGeneration_;
    if (repository.isEmpty() || !allowWorktreeChange())
        return;
    QVector<QuickPickItem> items{
        {"stash", tr("Stash & Checkout"),
         tr("Save tracked and untracked work; leave it in recovery")},
        {"migrate", tr("Migrate Changes"),
         tr("Save, switch, then reapply; retain recovery even on conflict")},
        {"backup", tr("Force Checkout (with Recovery Backup)"),
         tr("Back up tracked, untracked and ignored files before switching")}};
    QuickOpenDialog picker(tr("Checkout Blocked by Local Changes"),
                           tr("Choose recovery; Escape to cancel"), items, {}, this);
    if (picker.exec() != QDialog::Accepted || repository != git_->repository() ||
        generation != workspaceGeneration_)
        return;
    const auto selected = picker.selectedItemId();
    const auto recovery = selected == "stash"     ? workspace::GitCheckoutRecovery::Stash
                          : selected == "migrate" ? workspace::GitCheckoutRecovery::Migrate
                                                  : workspace::GitCheckoutRecovery::BackupAll;
    if (recovery == workspace::GitCheckoutRecovery::BackupAll &&
        ConfirmationDialog::ask(
            this, tr("Confirm Force Checkout"),
            tr("All current work, including ignored files, will be moved into a retained Git "
               "recovery copy before checkout. Large ignored directories can take time. No "
               "recovery copy will be deleted automatically. Continue?"),
            tr("Back Up and Checkout"), {}, tr("Cancel")) != ConfirmationChoice::Primary)
        return;
    // A refresh may have begun while the modal chooser was open. Wait for it,
    // and invalidate this request if the user switches repositories.
    auto* ready = new QTimer(this);
    ready->setInterval(50);
    connect(ready, &QTimer::timeout, this,
            [this, ready, repository, generation, ref, detached, recovery] {
                if (repository != git_->repository() || generation != workspaceGeneration_) {
                    ready->stop();
                    ready->deleteLater();
                    return;
                }
                if (git_->busy())
                    return;
                ready->stop();
                ready->deleteLater();
                if (detached)
                    git_->checkoutDetached(ref, recovery);
                else
                    git_->checkoutBranch(ref, recovery);
            });
    ready->start();
}
void SourceControlPanel::createBranch(bool from) {
    if (git_->busy() || git_->repository().isEmpty())
        return;
    if (from) {
        creatingFrom_ = true;
        chooseBranch();
        return;
    }
    const auto repository = git_->repository();
    const auto generation = workspaceGeneration_;
    if (const auto name = requestGitBranchName(this, repository, preferences_))
        if (repository == git_->repository() && allowWorktreeChange() &&
            generation == workspaceGeneration_)
            git_->createBranch(*name);
}
void SourceControlPanel::render() {
    emit gitStatusChanged(git_->repository(), git_->status());
    QStringList selected;
    const int scroll = changes_->verticalScrollBar()->value();
    for (auto* item : changes_->selectedItems())
        if (item->parent())
            selected.append(item->data(0, Qt::UserRole).toString() +
                            (item->data(0, Qt::UserRole + 1).toBool() ? "#staged" : "#working"));
    static_cast<ElidingLabel*>(summary_)->setFullText(
        findChild<QStackedWidget*>("scmViews")->currentIndex() == 1 ? tr("Graph")
        : git_->repository().isEmpty()                              ? QString()
                                       : QFileInfo(git_->repository()).fileName());
    summary_->setToolTip(git_->repository() + "\n" + git_->status().branch);
    // VS Code presents a welcome view in an opened non-repository folder, not
    // an empty repository toolbar followed by a full-width button.
    findChild<QWidget*>("sourceControlToolbar")
        ->setVisible(!git_->repository().isEmpty() || workspaceRoot_.isEmpty());
    findChild<QWidget*>("scmInputArea")->setVisible(!git_->repository().isEmpty());
    emptyWelcome_->setVisible(git_->repository().isEmpty());
    changes_->setVisible(!git_->repository().isEmpty());
    empty_->setText(workspaceRoot_.isEmpty()
                        ? tr("Open a folder containing a Git repository to view changes.")
                        : tr("The folder currently open doesn't have a Git repository. "
                             "You can initialize a repository which will enable source "
                             "control features powered by Git."));
    emptyDocs_->setVisible(!workspaceRoot_.isEmpty());
    initialize_->setVisible(git_->gitInstalled() && git_->repository().isEmpty() &&
                            !workspaceRoot_.isEmpty());
    QHash<QString, bool> expanded;
    for (int i = 0; i < changes_->topLevelItemCount(); ++i) {
        auto* group = changes_->topLevelItem(i);
        expanded.insert(group->data(0, Qt::UserRole).toString(), group->isExpanded());
    }
    changes_->clear();
    auto* staged = new QTreeWidgetItem(changes_, {tr("Staged Changes")});
    auto* unstaged = new QTreeWidgetItem(changes_, {tr("Changes")});
    auto* conflicts = new QTreeWidgetItem(changes_, {tr("Merge Changes")});
    for (const auto& change : git_->status().changes) {
        auto add = [&](QTreeWidgetItem* group, bool index, bool untracked) {
            auto* item = new QTreeWidgetItem(group, {change.path});
            item->setToolTip(0, change.path);
            item->setData(0, Qt::UserRole, change.path);
            item->setData(0, Qt::UserRole + 1, index);
            item->setData(0, Qt::UserRole + 2, untracked);
            item->setData(0, Qt::UserRole + 5, change.conflict);
            item->setData(0, Qt::UserRole + 3, change.originalPath);
            item->setData(0, Qt::UserRole + 4,
                          change.conflict ? QStringLiteral("!")
                          : untracked    ? QStringLiteral("U")
                                         : QString(QChar(index ? change.index : change.worktree)));
            item->setSelected(selected.contains(change.path + (index ? "#staged" : "#working")));
        };
        if (change.conflict) {
            add(conflicts, false, true);
            continue;
        }
        if (change.index != ' ' && change.index != '?')
            add(staged, true, false);
        if (change.worktree != ' ' || change.index == '?')
            add(unstaged, false, change.index == '?');
    }
    for (auto* group : {staged, unstaged, conflicts}) {
        const QString key = group->text(0);
        group->setData(0, Qt::UserRole, key);
        group->setExpanded(expanded.value(key, true));
        group->setText(0, key + QStringLiteral("  %1").arg(group->childCount()));
        group->setHidden(group->childCount() == 0);
    }
    changes_->verticalScrollBar()->setValue(scroll);
    updateActions();
}
void SourceControlPanel::updateActions() {
    bool staged = false, conflict = false;
    for (const auto& change : git_->status().changes) {
        staged |= change.index != ' ' && change.index != '?';
        conflict |= change.conflict;
    }
    commit_->setEnabled(!git_->busy() && staged && !conflict &&
                        !message_->toPlainText().trimmed().isEmpty());
    refresh_->setEnabled(!git_->busy());
}
void SourceControlPanel::showDiff() {
    auto* item = changes_->currentItem();
    if (!item || !item->parent())
        return;
    const auto path = item->data(0, Qt::UserRole).toString();
    if (item->data(0, Qt::UserRole + 5).toBool())
        emit openFileRequested(QDir(git_->repository()).filePath(path));
    else
        git_->compare(path, item->data(0, Qt::UserRole + 1).toBool());
}
void SourceControlPanel::chooseBranch() {
    if (git_->busy())
        return;
    choosingBranch_ = true;
    git_->listBranches();
}
void SourceControlPanel::chooseRemote(bool publish) {
    if (git_->busy())
        return;
    publishing_ = publish;
    choosingRemote_ = true;
    git_->listRemotes();
}
bool SourceControlPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == changes_->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        auto* item = changes_->itemAt(mouse->position().toPoint());
        if (mouse->button() == Qt::LeftButton && item && item->parent()) {
            const auto rect = changes_->visualItemRect(item);
            if (ChangeDelegate::actionRect(rect).contains(mouse->position().toPoint())) {
                if (!git_->busy()) {
                    changes_->clearSelection();
                    item->setSelected(true);
                    transfer(!item->data(0, Qt::UserRole + 1).toBool());
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
void SourceControlPanel::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        refreshThemedIcons(this, qApp->property("litecodeDarkTheme").toBool());
}
void SourceControlPanel::transfer(bool stage) {
    QStringList paths;
    for (auto* item : changes_->selectedItems()) {
        if (!item->parent() || (stage && item->data(0, Qt::UserRole + 1).toBool()) ||
            (!stage && !item->data(0, Qt::UserRole + 1).toBool()))
            continue;
        paths.append(item->data(0, Qt::UserRole).toString());
        const auto original = item->data(0, Qt::UserRole + 3).toString();
        if (!original.isEmpty())
            paths.append(original);
    }
    paths.removeDuplicates();
    if (!paths.isEmpty()) {
        notice_->hide();
        if (stage)
            git_->stage(paths);
        else
            git_->unstage(paths);
    }
}
} // namespace litecode::ui
