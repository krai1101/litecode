#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

class QAction;
class QMenu;
class QWidget;

namespace litecode::ui {

enum class PopupVerticalPlacement {
    BelowFirst,
    AboveFirst,
};

enum class PopupHorizontalPlacement {
    AlignLeft,
    AlignRight,
    AfterAnchor,
};

// Shared placement rules for every LiteCode-owned transient surface. Native
// operating-system dialogs remain managed by the platform.
class PopupPositioner final {
  public:
    [[nodiscard]] static QPoint clampTopLeft(const QSize& popupSize, QWidget* owner,
                                             const QPoint& desiredGlobal, int margin = 4);
    [[nodiscard]] static QPoint
    anchoredTopLeft(const QSize& popupSize, QWidget* anchor, QWidget* owner,
                    PopupVerticalPlacement vertical = PopupVerticalPlacement::BelowFirst,
                    PopupHorizontalPlacement horizontal = PopupHorizontalPlacement::AlignLeft,
                    int margin = 4);
    [[nodiscard]] static QPoint
    anchoredTopLeft(const QSize& popupSize, const QRect& anchorBounds, QWidget* owner,
                    PopupVerticalPlacement vertical = PopupVerticalPlacement::BelowFirst,
                    PopupHorizontalPlacement horizontal = PopupHorizontalPlacement::AlignLeft,
                    int margin = 4);
    static void popupMenu(QMenu& menu, QWidget* anchor, QWidget* owner,
                          PopupVerticalPlacement vertical, PopupHorizontalPlacement horizontal);
    [[nodiscard]] static QAction* execMenuAt(QMenu& menu, const QPoint& desiredGlobal,
                                             QWidget* owner);
    static void placeTopCentered(QWidget& popup, QWidget* owner, int topGap = 30, int edgeGap = 16);
    [[nodiscard]] static int boundedWidth(QWidget* owner, int preferredWidth, int edgeGap = 16);
    [[nodiscard]] static QSize boundedDialogSize(QWidget* owner, const QSize& preferred,
                                                 const QSize& minimum = QSize(320, 240),
                                                 int edgeGap = 24);
};

} // namespace litecode::ui
