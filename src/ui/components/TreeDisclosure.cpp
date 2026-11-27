#include "ui/components/TreeDisclosure.h"
#include "ui/ThemedIcon.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QTimer>
#include <QTreeView>
#include <utility>

namespace litecode::ui::components {
namespace {
class TreeDisclosure final : public QObject {
  public:
    TreeDisclosure(QTreeView& tree, std::function<bool(const QModelIndex&)> expandable)
        : QObject(&tree), tree_(tree), expandable_(std::move(expandable)) {
        tree_.setExpandsOnDoubleClick(false);
        tree_.viewport()->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease)
            return false;
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        const auto row = tree_.indexAt(mouse->position().toPoint()).siblingAtColumn(0);
        if (event->type() == QEvent::MouseButtonPress) {
            pressed_ = QPersistentModelIndex(row);
            expandedAtPress_ = row.isValid() && tree_.isExpanded(row);
        } else {
            const auto pressed = std::exchange(pressed_, {});
            if (pressed.isValid() && row == pressed && expandable_(row)) {
                // Qt handles its native chevron first. Apply the row action
                // only when that click did not already change the branch.
                QTimer::singleShot(0, this, [this, pressed, expanded = expandedAtPress_] {
                    if (pressed.isValid() && tree_.isExpanded(pressed) == expanded)
                        tree_.setExpanded(pressed, !expanded);
                });
            }
        }
        return false;
    }

  private:
    QTreeView& tree_;
    std::function<bool(const QModelIndex&)> expandable_;
    QPersistentModelIndex pressed_;
    bool expandedAtPress_{};
};
} // namespace

void enableTreeRowDisclosure(QTreeView& tree, std::function<bool(const QModelIndex&)> expandable) {
    new TreeDisclosure(tree, std::move(expandable));
}

void paintTreeRowHighlight(QPainter& painter, const QRect& row, const QColor& color) {
    if (color.isValid())
        painter.fillRect(row, color);
}

void paintTreeDisclosure(QPainter& painter, const QRect& branch, bool expanded, bool dark) {
    constexpr int extent = 16;
    const QRect iconRect(branch.right() - extent, branch.center().y() - extent / 2, extent, extent);
    themedIcon(expanded ? ":/icons/chevron-down.svg" : ":/icons/chevron-right.svg", dark)
        .paint(&painter, iconRect, Qt::AlignCenter);
}
} // namespace litecode::ui::components
