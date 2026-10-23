#include "ui/components/PopupSurface.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QDialog>
#include <QGraphicsDropShadowEffect>
#include <QVBoxLayout>

namespace litecode::ui {

PopupSurface::PopupSurface(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("popupSurface"));
    setProperty("uiComponent", QStringLiteral("popup"));
    setAttribute(Qt::WA_StyledBackground, true);
    contentLayout_ = new QVBoxLayout(this);
    contentLayout_->setContentsMargins(5, 5, 5, 5);
    contentLayout_->setSpacing(2);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(16);
    shadow->setOffset(0, 3);
    shadow->setColor(Theme::tokens(qApp->property("litecodeDarkTheme").toBool()).popupShadow);
    setGraphicsEffect(shadow);
}

QVBoxLayout* PopupSurface::contentLayout() const { return contentLayout_; }

void PopupSurface::configureDialog(QDialog& dialog) {
    dialog.setProperty("uiComponent", QStringLiteral("popupHost"));
    dialog.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

} // namespace litecode::ui
