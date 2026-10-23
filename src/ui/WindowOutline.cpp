#include "ui/WindowOutline.h"

#include "ui/Theme.h"

#include <QPaintEvent>
#include <QPainter>

namespace litecode::ui {

WindowOutline::WindowOutline(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}

void WindowOutline::setDarkTheme(bool dark) {
    dark_ = dark;
    update();
}

void WindowOutline::syncToWindow(bool maximized) {
    setVisible(!maximized);
    if (maximized || parentWidget() == nullptr)
        return;
    setGeometry(parentWidget()->rect());
    raise();
    update();
}

void WindowOutline::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setPen(Theme::tokens(dark_).windowOutline);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

} // namespace litecode::ui
