#include "ui/debug/DebugInspectorDelegates.h"
#include "editor/SyntaxTheme.h"
#include "ui/Theme.h"
#include "ui/components/TreeDisclosure.h"
#include <QApplication>
#include <QHeaderView>
#include <QPainter>
#include <QResizeEvent>
#include <QStyledItemDelegate>
namespace litecode::ui {
namespace {
class VariableValueDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(22); // VS Code's VariablesDelegate row height.
        return size;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QString name = index.data(debug_inspector::VariableNameRole).toString();
        if (name.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const auto theme = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
        const auto colors =
            editor::SyntaxTheme::palette(qApp->property("litecodeDarkTheme").toBool());
        painter->save();
        painter->setClipRect(option.rect);
        QFont font = option.font;
        font.setFamily(QStringLiteral("Consolas"));
        painter->setFont(font);
        const QFontMetrics metrics(font);
        const int baseline =
            option.rect.top() + (option.rect.height() + metrics.ascent() - metrics.descent()) / 2;
        const int x = option.rect.left() + 3;
        painter->setPen(colors.declaration);
        painter->drawText(x, baseline, name);
        const int separatorX = x + metrics.horizontalAdvance(name);
        const QString separator = QStringLiteral(" = ");
        painter->setPen(theme.foreground);
        painter->drawText(separatorX, baseline, separator);
        const QString value = index.data(debug_inspector::VariableValueRole).toString();
        bool numeric = false;
        value.toDouble(&numeric);
        painter->setPen(numeric                                          ? colors.number
                        : value.startsWith('[') || value.startsWith('{') ? theme.mutedForeground
                                                                         : theme.foreground);
        const int valueX = separatorX + metrics.horizontalAdvance(separator);
        painter->drawText(
            valueX, baseline,
            metrics.elidedText(value, Qt::ElideRight, qMax(0, option.rect.right() - valueX)));
        painter->restore();
    }
};

class LineBadgeDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QString text = index.data(Qt::DisplayRole).toString();
        if (text.isEmpty())
            return;
        painter->save();
        painter->setClipRect(option.rect);
        const QFontMetrics metrics(option.font);
        const int width =
            qMax(0, qMin(option.rect.width() - 2, metrics.horizontalAdvance(text) + 10));
        const QRectF badge(option.rect.center().x() - width / 2.0, option.rect.center().y() - 8.0,
                           width, 16);
        const bool dark = qApp->property("litecodeDarkTheme").toBool();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(dark ? "#424242" : "#e5e5e5"));
        painter->setRenderHint(QPainter::Antialiasing);
        painter->drawRoundedRect(badge, 8, 8);
        painter->setPen(Theme::tokens(dark).mutedForeground);
        painter->drawText(badge, Qt::AlignCenter, text);
        painter->restore();
    }
};

class InspectorRowDelegate : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(22);
        return size;
    }
};
class InsetNameDelegate final : public InspectorRowDelegate {
  public:
    InsetNameDelegate(QTreeWidget* tree, bool breakpoint)
        : InspectorRowDelegate(tree), tree_(tree), breakpoint_(breakpoint) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        auto adjusted = option;
        initStyleOption(&adjusted, index);
        const auto icon = adjusted.icon;
        adjusted.icon = {};
        adjusted.features &= ~QStyleOptionViewItem::HasDecoration;
        adjusted.rect.adjust(breakpoint_ ? 18 : 4, 0, 0, 0);
        tree_->style()->drawControl(QStyle::CE_ItemViewItem, &adjusted, painter, tree_);
        if (breakpoint_ && !icon.isNull())
            icon.paint(painter, QRect(option.rect.left(), option.rect.center().y() - 8, 16, 16));
    }
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        auto adjusted = option;
        adjusted.rect.adjust(18, 0, 0, 0);
        return InspectorRowDelegate::editorEvent(event, model, adjusted, index);
    }

  private:
    QTreeWidget* tree_;
    bool breakpoint_;
};
} // namespace
DebugInspectorTree::DebugInspectorTree(QWidget* parent) : components::Tree(parent) {
    setIndentation(18);
    setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTextElideMode(Qt::ElideRight);
    setUniformRowHeights(true);
    auto textFont = font();
    textFont.setPixelSize(13);
    setFont(textFont);
    setItemDelegate(new InspectorRowDelegate(this));
}
void DebugInspectorTree::drawBranches(QPainter* painter, const QRect& rect,
                                      const QModelIndex& index) const {
    const bool dark = qApp->property("litecodeDarkTheme").toBool();
    if (objectName() == "debugVariables" && index.parent().isValid()) {
        painter->save();
        painter->setPen(Theme::tokens(dark).border);
        const int guide = rect.left() + indentation() / 2;
        painter->drawLine(guide, rect.top(), guide, rect.bottom() + 1);
        painter->restore();
    }
    if (!index.data(debug_inspector::ShowSkippedFramesRole).toBool() && model()->hasChildren(index))
        components::paintTreeDisclosure(*painter, rect, isExpanded(index), dark);
}

void DebugInspectorTree::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    const auto theme = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
    const QRect row(0, option.rect.y(), viewport()->width(), option.rect.height());
    if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
        components::paintTreeRowHighlight(*painter, row,
            option.state & QStyle::State_Selected ? theme.listSelectionBackground
                                                  : theme.listHoverBackground);
    }

    // QTreeView draws a separate selection panel in the branch gutter. Paint
    // the disclosure and cells directly so the reusable highlight owns the row.
    const auto firstCell = visualRect(index.siblingAtColumn(0));
    auto branch = row;
    if (layoutDirection() == Qt::RightToLeft)
        branch.setLeft(firstCell.right() + 1);
    else
        branch.setRight(firstCell.left() - 1);
    if (branch.width() > 0)
        drawBranches(painter, branch, index);

    auto cell = option;
    cell.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
    const int cells = isFirstColumnSpanned(index.row(), index.parent()) ? 1 : columnCount();
    for (int column = 0; column < cells; ++column) {
        if (isColumnHidden(column))
            continue;
        const auto cellIndex = index.siblingAtColumn(column);
        cell.rect = visualRect(cellIndex);
        if (cellIndex.isValid() && cell.rect.width() > 0 && !indexWidget(cellIndex))
            itemDelegateForIndex(cellIndex)->paint(painter, cell, cellIndex);
    }
}

void DebugInspectorTree::resizeEvent(QResizeEvent* event) {
    components::Tree::resizeEvent(event);
    if (columnCount() != 3)
        return;
    const int width = viewport()->width();
    const int badge = qMin(36, width);
    const int detail = qMin(180, qMax(0, objectName() == "debugCallStack"
        ? (width - badge) / 4 : (width - badge) * 2 / 5));
    header()->setSectionResizeMode(QHeaderView::Fixed);
    header()->resizeSection(0, qMax(0, width - badge - detail));
    header()->resizeSection(1, detail);
    header()->resizeSection(2, badge);
}
void installDebugInspectorDelegates(QTreeWidget* variables, QTreeWidget* stack,
                                    QTreeWidget* breakpoints) {
    variables->setItemDelegate(new VariableValueDelegate(variables));
    stack->setItemDelegateForColumn(0, new InsetNameDelegate(stack, false));
    stack->setItemDelegateForColumn(2, new LineBadgeDelegate(stack));
    breakpoints->setItemDelegateForColumn(0, new InsetNameDelegate(breakpoints, true));
    breakpoints->setItemDelegateForColumn(2, new LineBadgeDelegate(breakpoints));
}
} // namespace litecode::ui
