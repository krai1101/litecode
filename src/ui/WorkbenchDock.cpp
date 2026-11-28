#include "ui/WorkbenchDock.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWidget>

namespace litecode::ui {
WorkbenchDock::WorkbenchDock(const QString& title, bool showRightBorder, QWidget* parent)
    : QDockWidget(title, parent) {
    // Hover propagates from child controls, including extension-provided bodies,
    // so the shared workbench resizer can expose its cursor over blank edges too.
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    if (!showRightBorder)
        return;

    rightBorder_ = new QWidget(this);
    rightBorder_->setObjectName(QStringLiteral("workspaceEdgeBorder"));
    rightBorder_->setAttribute(Qt::WA_TransparentForMouseEvents);
    rightBorder_->setAttribute(Qt::WA_StyledBackground);
}

void WorkbenchDock::resizeEvent(QResizeEvent* event) {
    QDockWidget::resizeEvent(event);
    positionRightBorder();
}

void WorkbenchDock::showEvent(QShowEvent* event) {
    QDockWidget::showEvent(event);
    positionRightBorder();
}
void WorkbenchDock::closeEvent(QCloseEvent* event) {
    QDockWidget::closeEvent(event);
    if (event->isAccepted())
        emit closedByUser();
}

void WorkbenchDock::positionRightBorder() {
    if (rightBorder_ != nullptr) {
        const auto* window = qobject_cast<QMainWindow*>(parentWidget());
        const bool leftEdge = window && window->dockWidgetArea(this) == Qt::RightDockWidgetArea;
        rightBorder_->setGeometry(leftEdge ? 0 : width() - 1, 0, 1, height());
        rightBorder_->raise();
    }
}

} // namespace litecode::ui
