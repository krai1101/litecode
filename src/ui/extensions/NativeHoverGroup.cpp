#include "ui/extensions/NativeHoverGroup.h"
#include <QApplication>
#include <QGraphicsOpacityEffect>
namespace litecode::ui {
NativeHoverGroup::NativeHoverGroup(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    connect(qApp, &QApplication::focusChanged, this, [this] { refresh(); });
}
NativeHoverGroup::~NativeHoverGroup() {
    // QWidget destroys focused children after derived members are gone.
    disconnect(qApp, nullptr, this, nullptr);
}
void NativeHoverGroup::addActions(QWidget* actions) {
    actions_.removeIf([this](const auto& item) { return !item || !isAncestorOf(item.data()); });
    if (actions_.contains(actions)) {
        refresh();
        return;
    }
    actions->setGraphicsEffect(new QGraphicsOpacityEffect(actions));
    actions_.append(actions);
    refresh();
}
void NativeHoverGroup::refresh() {
    actions_.removeIf([this](const auto& item) { return !item || !isAncestorOf(item.data()); });
    const auto* focused = QApplication::focusWidget();
    const bool reveal = hovered_ || (focused && isAncestorOf(focused));
    for (const auto& actions : actions_)
        if (actions) {
            if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(actions->graphicsEffect()))
                effect->setOpacity(reveal ? 1 : 0);
            actions->setAttribute(Qt::WA_TransparentForMouseEvents, !reveal);
        }
}
bool NativeHoverGroup::event(QEvent* event) {
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverMove) {
        hovered_ = true;
        refresh();
    } else if (event->type() == QEvent::HoverLeave) {
        hovered_ = false;
        refresh();
    }
    return QWidget::event(event);
}
void NativeHoverGroup::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    hovered_ = true;
    refresh();
}
void NativeHoverGroup::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    hovered_ = false;
    refresh();
}
void NativeHoverGroup::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();
}
} // namespace litecode::ui
