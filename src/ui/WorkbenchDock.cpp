#include "ui/WorkbenchDock.h"

#include <QResizeEvent>
#include <QShowEvent>
#include <QWidget>

namespace litecode::ui {
WorkbenchDock::WorkbenchDock(const QString& title, bool showRightBorder, QWidget* parent)
    : QDockWidget(title, parent) {
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

void WorkbenchDock::positionRightBorder() {
    if (rightBorder_ != nullptr) {
        rightBorder_->setGeometry(width() - 1, 0, 1, height());
        rightBorder_->raise();
    }
}

} // namespace litecode::ui
