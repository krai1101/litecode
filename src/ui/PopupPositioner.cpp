#include "ui/PopupPositioner.h"

#include <QApplication>
#include <QMenu>
#include <QScreen>
#include <QWidget>

#include <algorithm>

namespace litecode::ui {
namespace {

QRect ownerBounds(QWidget* owner) {
    if (owner) {
        const QPoint topLeft = owner->mapToGlobal(QPoint(0, 0));
        return QRect(topLeft, owner->size());
    }
    if (QScreen* screen = QGuiApplication::primaryScreen())
        return screen->availableGeometry();
    return {};
}

int clampedCoordinate(int value, int minimum, int maximum) {
    return std::clamp(value, minimum, std::max(minimum, maximum));
}

} // namespace

QPoint PopupPositioner::clampTopLeft(const QSize& popupSize, QWidget* owner,
                                     const QPoint& desiredGlobal, int margin) {
    const QRect bounds = ownerBounds(owner);
    if (!bounds.isValid())
        return desiredGlobal;
    const int minimumX = bounds.left() + margin;
    const int minimumY = bounds.top() + margin;
    const int maximumX = bounds.right() - margin - popupSize.width() + 1;
    const int maximumY = bounds.bottom() - margin - popupSize.height() + 1;
    return {clampedCoordinate(desiredGlobal.x(), minimumX, maximumX),
            clampedCoordinate(desiredGlobal.y(), minimumY, maximumY)};
}

QPoint PopupPositioner::anchoredTopLeft(const QSize& popupSize, QWidget* anchor, QWidget* owner,
                                        PopupVerticalPlacement vertical,
                                        PopupHorizontalPlacement horizontal, int margin) {
    if (!anchor)
        return clampTopLeft(popupSize, owner, {}, margin);

    const QRect anchorBounds(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    return anchoredTopLeft(popupSize, anchorBounds, owner, vertical, horizontal, margin);
}

QPoint PopupPositioner::anchoredTopLeft(const QSize& popupSize, const QRect& anchorBounds,
                                        QWidget* owner, PopupVerticalPlacement vertical,
                                        PopupHorizontalPlacement horizontal, int margin) {
    const QRect bounds = ownerBounds(owner);
    int x = anchorBounds.left();
    if (horizontal == PopupHorizontalPlacement::AlignRight)
        x = anchorBounds.right() - popupSize.width() + 1;
    else if (horizontal == PopupHorizontalPlacement::AfterAnchor)
        x = anchorBounds.right() + 1;

    const int below = anchorBounds.bottom() + 1;
    const int above = anchorBounds.top() - popupSize.height();
    const bool belowFits =
        !bounds.isValid() || below + popupSize.height() <= bounds.bottom() - margin;
    const bool aboveFits = !bounds.isValid() || above >= bounds.top() + margin;
    int y = below;
    if (vertical == PopupVerticalPlacement::BelowFirst)
        y = belowFits || !aboveFits ? below : above;
    else
        y = aboveFits || !belowFits ? above : below;
    return clampTopLeft(popupSize, owner, QPoint(x, y), margin);
}

void PopupPositioner::popupMenu(QMenu& menu, QWidget* anchor, QWidget* owner,
                                PopupVerticalPlacement vertical,
                                PopupHorizontalPlacement horizontal) {
    menu.ensurePolished();
    menu.adjustSize();
    menu.popup(anchoredTopLeft(menu.sizeHint(), anchor, owner, vertical, horizontal));
}

QAction* PopupPositioner::execMenuAt(QMenu& menu, const QPoint& desiredGlobal, QWidget* owner) {
    menu.ensurePolished();
    menu.adjustSize();
    return menu.exec(clampTopLeft(menu.sizeHint(), owner, desiredGlobal));
}

void PopupPositioner::placeTopCentered(QWidget& popup, QWidget* owner, int topGap, int edgeGap) {
    if (!owner)
        return;
    const QPoint desired = owner->mapToGlobal(QPoint((owner->width() - popup.width()) / 2, topGap));
    popup.move(clampTopLeft(popup.size(), owner, desired, edgeGap));
}

int PopupPositioner::boundedWidth(QWidget* owner, int preferredWidth, int edgeGap) {
    if (!owner)
        return preferredWidth;
    return std::max(1, std::min(preferredWidth, owner->width() - (2 * edgeGap)));
}

QSize PopupPositioner::boundedDialogSize(QWidget* owner, const QSize& preferred,
                                         const QSize& minimum, int edgeGap) {
    if (!owner)
        return preferred.expandedTo(minimum);
    const QSize available(std::max(1, owner->width() - (2 * edgeGap)),
                          std::max(1, owner->height() - (2 * edgeGap)));
    const QSize effectiveMinimum(std::min(minimum.width(), available.width()),
                                 std::min(minimum.height(), available.height()));
    return preferred.boundedTo(available).expandedTo(effectiveMinimum);
}

} // namespace litecode::ui
