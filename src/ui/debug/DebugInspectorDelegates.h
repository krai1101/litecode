#pragma once
#include "ui/components/Controls.h"
namespace litecode::ui {
namespace debug_inspector {
enum Role {
    ReferenceRole = Qt::UserRole,
    PathRole,
    LineRole,
    ExceptionFilterRole,
    VariableNameRole,
    VariableValueRole,
    FrameOriginRole,
    ShowSkippedFramesRole
};
}
// A shared, compact tree for debugger inspection panes. Width is allocated
// between the primary label, optional file detail and trailing line badge.
class DebugInspectorTree final : public components::Tree {
  public:
    explicit DebugInspectorTree(QWidget* parent);

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void drawBranches(QPainter* painter, const QRect& rect, const QModelIndex& index) const override;
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};
void installDebugInspectorDelegates(QTreeWidget* variables, QTreeWidget* stack,
                                    QTreeWidget* breakpoints);
} // namespace litecode::ui
