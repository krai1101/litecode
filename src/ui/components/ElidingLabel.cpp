#include "ui/components/ElidingLabel.h"
#include <QPainter>
namespace litecode::ui::components {
ElidingLabel::ElidingLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setTextFormat(Qt::PlainText);
    setWordWrap(false);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}
QSize ElidingLabel::sizeHint() const {
    return QSize(fontMetrics().horizontalAdvance(text()), fontMetrics().height() + 4);
}
QSize ElidingLabel::minimumSizeHint() const { return QSize(0, fontMetrics().height() + 4); }
void ElidingLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
                     fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
}
} // namespace litecode::ui::components
