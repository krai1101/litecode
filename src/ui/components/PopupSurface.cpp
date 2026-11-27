#include "ui/components/PopupSurface.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QDialog>
#include <QGraphicsDropShadowEffect>
#include <QScreen>
#include <QVBoxLayout>

namespace litecode::ui {

PopupSurface::PopupSurface(QWidget* parent, qreal shadowStrength) : QFrame(parent) {
    setObjectName(QStringLiteral("popupSurface"));
    setProperty("uiComponent", QStringLiteral("popup"));
    setAttribute(Qt::WA_StyledBackground, true);
    contentLayout_ = new QVBoxLayout(this);
    contentLayout_->setContentsMargins(5, 5, 5, 5);
    contentLayout_->setSpacing(2);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(16);
    shadow->setOffset(0, 3);
    auto shadowColor = Theme::tokens(qApp->property("litecodeDarkTheme").toBool()).popupShadow;
    shadowColor.setAlpha(qRound(shadowColor.alpha() * shadowStrength));
    shadow->setColor(shadowColor);
    setGraphicsEffect(shadow);
}

QVBoxLayout* PopupSurface::contentLayout() const { return contentLayout_; }

QRect PopupSurface::anchoredGeometry(const QWidget& anchor, QSize desired, bool withinWindow,
                                     bool preferAbove, Qt::AlignmentFlag horizontal) {
    auto* screen = anchor.screen();
    if (!screen)
        return {};
    auto available = screen->availableGeometry();
    const QWidget* owner = anchor.window();
    // A selector inside another popup still belongs to the application window.
    // Clamping to the parent popup would truncate nested model/choice menus.
    while (owner && (owner->windowFlags() & Qt::WindowType_Mask) == Qt::Popup &&
           owner->parentWidget())
        owner = owner->parentWidget()->window();
    if (withinWindow && owner)
        available = available.intersected(
            QRect(owner->mapToGlobal(QPoint{}), owner->size()).adjusted(4, 4, -4, -4));
    if (available.isEmpty())
        return {};
    const auto below = anchor.mapToGlobal(QPoint(0, anchor.height() + 1));
    const auto above = anchor.mapToGlobal(QPoint(0, -1));
    const int belowSpace = qMax(0, available.bottom() - below.y() + 1);
    const int aboveSpace = qMax(0, above.y() - available.top() + 1);
    const bool upwards = preferAbove ? aboveSpace >= qMin(desired.height(), belowSpace)
                                     : belowSpace < desired.height() && aboveSpace > belowSpace;
    const int width = qBound(1, desired.width(), available.width());
    const int height = qBound(1, desired.height(), qMax(1, upwards ? aboveSpace : belowSpace));
    const int anchorX = horizontal == Qt::AlignRight ? below.x() + anchor.width() - width
                                                     : below.x();
    const int x = qBound(available.left(), anchorX, available.right() - width + 1);
    const int y = qBound(available.top(), upwards ? above.y() - height : below.y(),
                         available.bottom() - height + 1);
    return QRect(x, y, width, height);
}

void PopupSurface::configureDialog(QDialog& dialog) {
    dialog.setProperty("uiComponent", QStringLiteral("popupHost"));
    dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

} // namespace litecode::ui
