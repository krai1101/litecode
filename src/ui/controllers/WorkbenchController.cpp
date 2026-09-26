#include "ui/controllers/WorkbenchController.h"

#include <QFileInfo>

namespace litecode::ui {

WorkbenchController::WorkbenchController(QObject* parent) : QObject(parent) {}

QString WorkbenchController::workspacePath() const { return workspacePath_; }

bool WorkbenchController::hasWorkspace() const { return !workspacePath_.isEmpty(); }

SidebarView WorkbenchController::sidebarView() const { return sidebarView_; }

BottomPanelView WorkbenchController::bottomPanelView() const { return bottomPanelView_; }

bool WorkbenchController::primarySidebarVisible() const { return primarySidebarVisible_; }

bool WorkbenchController::bottomPanelVisible() const { return bottomPanelVisible_; }

void WorkbenchController::setWorkspacePath(const QString& workspacePath) {
    const QString resolved =
        workspacePath.isEmpty() ? QString{} : QFileInfo(workspacePath).absoluteFilePath();
    if (resolved == workspacePath_)
        return;
    workspacePath_ = resolved;
    emit workspaceChanged(workspacePath_, QFileInfo(workspacePath_).fileName());
}

void WorkbenchController::showSidebar(SidebarView view) {
    const bool changed = sidebarView_ != view || !primarySidebarVisible_;
    sidebarView_ = view;
    primarySidebarVisible_ = true;
    if (changed)
        emit sidebarChanged(sidebarView_, primarySidebarVisible_);
}

void WorkbenchController::setPrimarySidebarVisible(bool visible) {
    if (primarySidebarVisible_ == visible)
        return;
    primarySidebarVisible_ = visible;
    emit sidebarChanged(sidebarView_, primarySidebarVisible_);
}

void WorkbenchController::togglePrimarySidebar() {
    setPrimarySidebarVisible(!primarySidebarVisible_);
}

void WorkbenchController::showBottomPanel(BottomPanelView view) {
    const bool changed = bottomPanelView_ != view || !bottomPanelVisible_;
    bottomPanelView_ = view;
    bottomPanelVisible_ = true;
    if (changed)
        emit bottomPanelChanged(bottomPanelView_, bottomPanelVisible_);
}

void WorkbenchController::setBottomPanelVisible(bool visible) {
    if (bottomPanelVisible_ == visible)
        return;
    bottomPanelVisible_ = visible;
    emit bottomPanelChanged(bottomPanelView_, bottomPanelVisible_);
}

void WorkbenchController::toggleBottomPanel() { setBottomPanelVisible(!bottomPanelVisible_); }

} // namespace litecode::ui
