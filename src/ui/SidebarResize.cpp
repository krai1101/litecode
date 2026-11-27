#include "ui/SidebarService.h"
#include <QDockWidget>
#include <QMainWindow>
namespace litecode::ui {
bool SidebarService::resizeFromDrag(QDockWidget& dock, int requestedWidth, int initialWidth) {
    if (stopped_) return false;
    for (const auto place : {Location::Primary, Location::Secondary}) {
        auto& part = host(place);
        if (part.dock != &dock) continue;
        const int minimum = qMax(dock.minimumWidth(), dock.minimumSizeHint().width());
        if (dock.isHidden()) {
            // A live drag remains attached to the hidden sidebar. Require its
            // minimum width before reopening, avoiding chatter at the snap point.
            if (requestedWidth < minimum) return true;
            part.collapsedWidth = 0;
            setVisible(place, true);
            window_.resizeDocks({&dock}, {requestedWidth}, Qt::Horizontal);
            return true;
        }
        if (requestedWidth < minimum / 2) {
            part.collapsedWidth = qMax(minimum, initialWidth);
            setVisible(place, false);
        } else {
            window_.resizeDocks({&dock}, {qMax(minimum, requestedWidth)}, Qt::Horizontal);
        }
        return true;
    }
    return false;
}
void SidebarService::restoreCollapsedWidth(Location place) {
    auto& part = host(place);
    if (part.collapsedWidth <= 0 || part.dock->isHidden()) return;
    const int width = part.collapsedWidth;
    part.collapsedWidth = 0;
    window_.resizeDocks({part.dock}, {width}, Qt::Horizontal);
}
}
