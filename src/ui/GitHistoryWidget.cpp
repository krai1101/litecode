#include "ui/GitHistoryWidget.h"
#include "ui/FileIconTheme.h"
#include "ui/GitReferenceBadges.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/Controls.h"
#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace litecode::ui {
namespace {
constexpr int Id = Qt::UserRole, Parent = Id + 1, Path = Id + 2, OldPath = Id + 3, Status = Id + 4,
              Loaded = Id + 5, Inputs = Id + 6, Outputs = Id + 7, Lane = Id + 8, Author = Id + 9,
              References = Id + 10, Kind = Id + 11;
class HistoryDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {0, 22};
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto theme = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
        painter->save();
        painter->setClipRect(option.rect);
        const bool selected = option.state & QStyle::State_Selected;
        painter->fillRect(option.rect, selected ? theme.listSelectionBackground
                                       : (option.state & QStyle::State_MouseOver)
                                           ? theme.listHoverBackground
                                           : theme.sidebarSurface);
        auto font = option.font;
        font.setPixelSize(13);
        painter->setFont(font);
        const QFontMetrics metrics(font);
        const auto input = index.data(Inputs).toStringList(),
                   output = index.data(Outputs).toStringList();
        auto x = [&](int lane) { return option.rect.left() + 10 + 14 * lane; };
        const int mid = option.rect.center().y();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(theme.accent, 2));
        const bool file = index.parent().isValid();
        const auto kind = index.data(Kind).toString();
        if (!kind.isEmpty()) {
            themedIcon(kind == "incoming" ? ":/icons/arrow-down.svg" : ":/icons/arrow-up.svg",
                       qApp->property("litecodeDarkTheme").toBool())
                .paint(painter, QRect(x(0) - 8, option.rect.top() + 3, 16, 16));
        } else if (file) {
            for (int lane = 0; lane < output.size(); ++lane)
                painter->drawLine(x(lane), option.rect.top(), x(lane), option.rect.bottom() + 1);
        } else {
            const auto id = index.data(Id).toString();
            const int node = index.data(Lane).toInt();
            for (int lane = 0; lane < input.size(); ++lane) {
                const int target = input[lane] == id ? node : output.indexOf(input[lane]);
                if (target < 0)
                    continue;
                QPainterPath path;
                path.moveTo(x(lane), option.rect.top());
                path.lineTo(x(input[lane] == id ? node : target),
                            input[lane] == id ? mid : option.rect.bottom() + 1);
                painter->drawPath(path);
            }
            const auto parents = index.data(Parent).toString().split(' ', Qt::SkipEmptyParts);
            for (const auto& parent : parents) {
                const int target = output.indexOf(parent);
                if (target >= 0)
                    painter->drawLine(x(node), mid, x(target), option.rect.bottom() + 1);
            }
            const auto refs = index.data(References).toString().split(", ");
            const bool current = refs.contains("HEAD") ||
                                 std::any_of(refs.begin(), refs.end(), [](const QString& ref) {
                                     return ref.startsWith("HEAD -> ");
                                 });
            painter->setBrush(current ? theme.sidebarSurface : theme.accent);
            painter->drawEllipse(QPoint(x(node), mid), current ? 5 : 4, current ? 5 : 4);
        }
        painter->setRenderHint(QPainter::Antialiasing, false);
        int start = x(qMax(1, qMax(input.size(), output.size())) - 1) + 12;
        int end = option.rect.right() - 8;
        if (file) {
            const auto path = index.data(Path).toString();
            fileIconForPath(path).paint(painter, QRect(start, option.rect.top() + 3, 16, 16));
            start += 23;
            const auto status = index.data(Status).toString();
            painter->setPen(status == "A"   ? theme.success
                            : status == "D" ? theme.error
                                            : theme.warning);
            painter->drawText(QRect(end - 14, option.rect.top(), 14, option.rect.height()),
                              Qt::AlignVCenter, status);
            end -= 23;
            painter->setPen(selected ? theme.listSelectionForeground : theme.foreground);
            const auto name = QFileInfo(path).fileName();
            const auto shown = metrics.elidedText(name, Qt::ElideRight, qMax(0, end - start));
            painter->drawText(
                QRect(start, option.rect.top(), qMax(0, end - start), option.rect.height()),
                Qt::AlignVCenter, shown);
            start += metrics.horizontalAdvance(shown) + 8;
            painter->setPen(theme.mutedForeground);
            const auto directory = QFileInfo(path).path();
            if (directory != ".")
                painter->drawText(
                    QRect(start, option.rect.top(), qMax(0, end - start), option.rect.height()),
                    Qt::AlignVCenter,
                    metrics.elidedText(directory, Qt::ElideRight, qMax(0, end - start)));
        } else {
            end = paintGitReferenceBadges(*painter, option.rect, metrics,
                                          gitReferenceBadges(index.data(References).toString()),
                                          theme, start, end);
            const int available = qMax(0, end - start);
            const auto author = index.data(Author).toString();
            // Keep authors visible even when the subject is long, without wasting space
            // on short subjects. All overflow is trailing, never middle-elided.
            const int authorWidth =
                author.isEmpty() ? 0 : qMin(metrics.horizontalAdvance(author), available / 3);
            const auto subject =
                metrics.elidedText(index.data().toString(), Qt::ElideRight,
                                   qMax(0, available - authorWidth - (authorWidth ? 8 : 0)));
            painter->setPen(selected ? theme.listSelectionForeground : theme.foreground);
            painter->drawText(
                QRect(start, option.rect.top(), qMax(0, end - start), option.rect.height()),
                Qt::AlignVCenter, subject);
            start += metrics.horizontalAdvance(subject) + 8;
            painter->setPen(theme.mutedForeground);
            painter->drawText(
                QRect(start, option.rect.top(), qMax(0, end - start), option.rect.height()),
                Qt::AlignVCenter,
                metrics.elidedText(index.data(Author).toString(), Qt::ElideRight,
                                   qMax(0, end - start)));
        }
        painter->restore();
    }
};
} // namespace
GitHistoryWidget::GitHistoryWidget(QWidget* parent) : components::SidebarView(parent) {
    setObjectName("gitHistoryWidget");
    git_ = new workspace::GitService(this);
    auto* layout = contentLayout();
    notice_ = new QLabel(this);
    notice_->setWordWrap(true);
    notice_->setObjectName("scmGraphNotice");
    notice_->setContentsMargins(components::SidebarView::controlMargins());
    notice_->hide();
    layout->addWidget(notice_);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName("gitHistory");
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(false);
    tree_->setIndentation(0);
    tree_->setUniformRowHeights(true);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setMouseTracking(true);
    tree_->setItemDelegate(new HistoryDelegate(tree_));
    TransientScrollBars::install(tree_);
    layout->addWidget(tree_, 1);
    auto* more = new components::Button(tr("Load more commits"), this);
    more_ = more;
    more_->hide();
    layout->addWidget(more);
    connect(more, &QPushButton::clicked, this, [this] {
        limit_ = qMin(limit_ + 100, 2000);
        schedule([this] { loadHistory(); });
    });
    connect(git_, &workspace::GitService::changed, this, [this] {
        if (git_->repository().isEmpty()) {
            tree_->clear();
            notice_->setText(tr("Open a Git repository to browse commits."));
            notice_->show();
        } else
            schedule([this] { loadHistory(); });
    });
    connect(git_, &workspace::GitService::busyChanged, this, [this](bool busy) {
        if (!busy)
            QTimer::singleShot(0, this, [this] {
                if (!git_->busy() && !queue_.isEmpty())
                    queue_.dequeue()();
            });
    });
    connect(git_, &workspace::GitService::error, this, [this](const QString& error) {
        notice_->setText(error);
        notice_->show();
        if (waitingForDivergence_ && requestedGeneration_ == generation_) {
            waitingForDivergence_ = false;
            render(commits_);
        }
    });
    connect(git_, &workspace::GitService::historyReady, this,
            [this](const QVector<workspace::GitCommit>& commits) {
                if (requestedGeneration_ != generation_)
                    return;
                commits_ = commits;
                divergence_ = {};
                if (!git_->repository().isEmpty() && (automatic_ || references_.isEmpty())) {
                    waitingForDivergence_ = true;
                    schedule([this] { git_->listDivergence(); });
                } else
                    render(commits);
            });
    connect(git_, &workspace::GitService::divergenceReady, this,
            [this](const workspace::GitDivergence& divergence) {
                if (requestedGeneration_ != generation_)
                    return;
                waitingForDivergence_ = false;
                divergence_ = divergence;
                render(commits_);
            });
    connect(git_, &workspace::GitService::branchesReady, this,
            [this](const QVector<workspace::GitBranch>& branches) {
                if (!filterAnchor_)
                    return;
                auto* anchor = filterAnchor_.data();
                filterAnchor_ = nullptr;
                components::Menu menu(this);
                menu.setMinimumWidth(240);
                auto* automatic = menu.addAction(tr("Auto"), this, [this] { setFilter({}, true); });
                automatic->setCheckable(true);
                automatic->setChecked(automatic_);
                auto* all =
                    menu.addAction(tr("All References"), this, [this] { setFilter({}, false); });
                all->setCheckable(true);
                all->setChecked(!automatic_ && references_.isEmpty());
                menu.addSeparator();
                for (const auto& branch : branches) {
                    auto* action =
                        menu.addAction(branch.tag ? tr("Tag: %1").arg(branch.name) : branch.name);
                    action->setCheckable(true);
                    action->setChecked(!automatic_ && references_.contains(branch.ref));
                    connect(action, &QAction::triggered, this,
                            [this, ref = branch.ref](bool checked) {
                                auto references = automatic_ ? QStringList() : references_;
                                if (checked)
                                    references.append(ref);
                                else
                                    references.removeAll(ref);
                                setFilter(references, false);
                            });
                }
                menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
            });
    connect(git_, &workspace::GitService::comparisonReady, this,
            &GitHistoryWidget::comparisonRequested);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const auto id = item->data(0, Id).toString();
        const auto parent =
            item->data(0, Parent).toString().split(' ', Qt::SkipEmptyParts).value(0);
        if (item->parent()) {
            workspace::GitCommitFile file{item->data(0, Path).toString(),
                                          item->data(0, OldPath).toString(),
                                          item->data(0, Status).toString().front().toLatin1()};
            schedule([this, id, parent, file] { git_->compareCommitFile(id, parent, file); });
        } else {
            item->setExpanded(!item->isExpanded());
            if (!item->data(0, Loaded).toBool())
                schedule([this, id, parent] { git_->listCommitFiles(id, parent); });
        }
    });
    connect(
        git_, &workspace::GitService::revisionFilesReady, this,
        [this](const QString& id, const QString& parent,
               const QVector<workspace::GitCommitFile>& files) {
            for (int row = 0; row < tree_->topLevelItemCount(); ++row) {
                auto* commit = tree_->topLevelItem(row);
                if (commit->data(0, Id).toString() != id ||
                    commit->data(0, Parent).toString().split(' ', Qt::SkipEmptyParts).value(0) !=
                        parent)
                    continue;
                qDeleteAll(commit->takeChildren());
                commit->setData(0, Loaded, true);
                for (const auto& file : files) {
                    auto* item = new QTreeWidgetItem(commit, {file.path});
                    item->setData(0, Id, id);
                    item->setData(0, Parent, commit->data(0, Parent));
                    item->setData(0, Path, file.path);
                    item->setData(0, OldPath, file.originalPath);
                    item->setData(0, Status, QString(QChar(file.status)));
                    item->setData(0, Outputs, commit->data(0, Outputs));
                    item->setToolTip(0, file.path);
                }
            }
        });
}
void GitHistoryWidget::schedule(std::function<void()> action) {
    if (git_->busy())
        queue_.enqueue(std::move(action));
    else
        action();
}
void GitHistoryWidget::setWorkspace(const QString& path) {
    ++generation_;
    waitingForDivergence_ = false;
    commits_.clear();
    divergence_ = {};
    filterAnchor_ = nullptr;
    references_.clear();
    automatic_ = true;
    emit filterChanged();
    queue_.clear();
    tree_->clear();
    limit_ = 100;
    git_->setWorkspace(path);
}
void GitHistoryWidget::loadHistory() {
    requestedGeneration_ = generation_;
    git_->listHistory(limit_, references_, automatic_);
}
void GitHistoryWidget::setFilter(const QStringList& references, bool automatic) {
    ++generation_;
    waitingForDivergence_ = false;
    queue_.clear();
    references_ = references;
    automatic_ = automatic;
    limit_ = 100;
    emit filterChanged();
    schedule([this] { loadHistory(); });
}
QString GitHistoryWidget::filterLabel() const {
    return automatic_              ? tr("Auto")
           : references_.isEmpty() ? tr("All")
                                   : tr("%1 Refs").arg(references_.size());
}
void GitHistoryWidget::chooseFilter(QWidget* anchor) {
    if (git_->repository().isEmpty())
        return;
    filterAnchor_ = anchor;
    schedule([this] { git_->listBranches(); });
}
void GitHistoryWidget::revealCurrentCommit() {
    for (int row = 0; row < tree_->topLevelItemCount(); ++row) {
        auto* item = tree_->topLevelItem(row);
        const auto refs = item->data(0, References).toString().split(", ");
        if (refs.contains("HEAD") || std::any_of(refs.begin(), refs.end(), [](const QString& ref) {
                return ref.startsWith("HEAD -> ");
            })) {
            tree_->setCurrentItem(item);
            tree_->scrollToItem(item);
            return;
        }
    }
}
void GitHistoryWidget::navigateCommit(int direction) {
    auto* current = tree_->currentItem();
    if (current && current->parent())
        current = current->parent();
    const int row = tree_->indexOfTopLevelItem(current);
    const int next = row < 0 ? 0 : row + direction;
    if (next >= 0 && next < tree_->topLevelItemCount()) {
        tree_->setCurrentItem(tree_->topLevelItem(next));
        tree_->scrollToItem(tree_->currentItem());
    }
}
void GitHistoryWidget::reload() {
    schedule([this] { git_->refresh(); });
}
void GitHistoryWidget::render(const QVector<workspace::GitCommit>& commits) {
    more_->setVisible(commits.size() >= limit_ && limit_ < 2000);
    QStringList expanded;
    const auto expansionKey = [](const QTreeWidgetItem* item) {
        return item->data(0, Id).toString() + "|" +
               item->data(0, Parent).toString().split(' ', Qt::SkipEmptyParts).value(0) + "|" +
               item->data(0, Kind).toString();
    };
    for (int row = 0; row < tree_->topLevelItemCount(); ++row)
        if (tree_->topLevelItem(row)->isExpanded())
            expanded.append(expansionKey(tree_->topLevelItem(row)));
    tree_->clear();
    notice_->setVisible(commits.isEmpty());
    notice_->setText(tr("No commits yet."));
    const auto placeholder = [&](const QString& kind, int count, const QString& target) {
        if (count <= 0)
            return;
        auto* item = new QTreeWidgetItem(tree_, {kind == "incoming"
                                                     ? tr("Incoming Changes (%1)").arg(count)
                                                     : tr("Outgoing Changes (%1)").arg(count)});
        item->setData(0, Kind, kind);
        item->setData(0, Id, target);
        item->setData(0, Parent, divergence_.mergeBase);
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        item->setToolTip(0, divergence_.mergeBase.isEmpty()
                                ? tr("No shared ancestor; compare against the empty tree.")
                                : tr("Compare this branch with the common ancestor."));
        if (expanded.contains(expansionKey(item))) {
            item->setExpanded(true);
            schedule([this, target, base = divergence_.mergeBase] {
                git_->listCommitFiles(target, base);
            });
        }
    };
    placeholder("incoming", divergence_.incoming, divergence_.upstream);
    placeholder("outgoing", divergence_.outgoing, divergence_.head);
    QStringList lanes;
    for (const auto& commit : commits) {
        auto input = lanes;
        int node = input.indexOf(commit.id);
        if (node < 0) {
            node = input.size();
            input.append(commit.id);
        }
        auto output = input;
        output.removeAt(node);
        for (int parent = 0; parent < commit.parents.size(); ++parent)
            if (!output.contains(commit.parents[parent]))
                output.insert(qMin(node + parent, static_cast<int>(output.size())),
                              commit.parents[parent]);
        auto* item = new QTreeWidgetItem(tree_, {commit.subject});
        item->setData(0, Author, commit.author);
        item->setData(0, References, commit.references);
        item->setData(0, Id, commit.id);
        item->setData(0, Parent, commit.parents.join(' '));
        item->setData(0, Inputs, input);
        item->setData(0, Outputs, output);
        item->setData(0, Lane, node);
        item->setToolTip(0, commit.id + "\n" + commit.references + "\n" + commit.subject + "\n" +
                                commit.author);
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        if (expanded.contains(expansionKey(item))) {
            item->setExpanded(true);
            schedule([this, id = commit.id, parent = commit.parents.value(0)] {
                git_->listCommitFiles(id, parent);
            });
        }
        lanes = output;
    }
}
} // namespace litecode::ui
