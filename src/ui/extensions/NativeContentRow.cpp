#include "ui/extensions/NativeContentRow.h"
#include <QLayout>
#include <QResizeEvent>
#include <QTimer>
namespace litecode::ui {
QSize NativeContentRow::minimumSizeHint() const {
    auto size = QWidget::minimumSizeHint();
    if (property("nativeTrailingActions").toBool()) size.setWidth(0);
    return size;
}
bool NativeContentRow::event(QEvent* event) {
    const bool handled = QWidget::event(event);
    if (property("nativeTrailingActions").toBool() && layout() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)) {
        // Keep action captions intact; any overflow is clipped by this row,
        // rather than becoming the minimum width of the entire card.
        auto bounds = contentsRect();
        bounds.setWidth(qMax(bounds.width(), layout()->minimumSize().width()));
        if (layout()->geometry() != bounds) layout()->setGeometry(bounds);
    }
    return handled;
}
QSize NativeContentRow::sizeHint() const {
    auto size = QWidget::sizeHint();
    if (layout() && layout()->hasHeightForWidth() && width() > 0)
        size.setHeight(layout()->heightForWidth(width()));
    return size;
}
void NativeContentRow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (event->size().width() != event->oldSize().width())
        QTimer::singleShot(0, this, [this] { updateGeometry(); });
}
} // namespace litecode::ui
