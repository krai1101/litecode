#include "ui/components/ShimmerLabel.h"
#include <QPainter>
#include <QStyle>
#include <cmath>
namespace litecode::ui::components {
ShimmerLabel::ShimmerLabel(const QString& text, QWidget* parent)
    : ElidingLabel(text, parent), animation_(this) {
    animation_.setInterval(2000 / 48);
    connect(&animation_, &QTimer::timeout, this, [this] {
        if (!visibleRegion().isEmpty())
            update();
    });
}
void ShimmerLabel::showEvent(QShowEvent* event) {
    ElidingLabel::showEvent(event);
    clock_.start();
    if (style()->styleHint(QStyle::SH_Widget_Animate, nullptr, this))
        animation_.start();
}
void ShimmerLabel::hideEvent(QHideEvent* event) {
    animation_.stop();
    ElidingLabel::hideEvent(event);
}
void ShimmerLabel::paintEvent(QPaintEvent* event) {
    if (!animation_.isActive()) {
        ElidingLabel::paintEvent(event);
        return;
    }
    const auto bounds = contentsRect();
    const auto display = fontMetrics().elidedText(text(), Qt::ElideRight, bounds.width());
    const auto extent = qMax(1, fontMetrics().horizontalAdvance(display));
    // Match the reference's two-second, 48-step horizontal text sweep.
    const qreal phase = std::floor((clock_.elapsed() % 2000) * 48.0 / 2000) / 48;
    const qreal band = extent * 0.5;
    const qreal start = bounds.left() - band + phase * (extent + band);
    const auto base = palette().color(QPalette::WindowText);
    auto highlight = base;
    highlight.setAlphaF(base.alphaF() * 0.2);
    QLinearGradient gradient(start, 0, start + band, 0);
    gradient.setColorAt(0, base);
    gradient.setColorAt(0.4, highlight);
    gradient.setColorAt(0.6, highlight);
    gradient.setColorAt(1, base);
    QPainter painter(this);
    painter.setPen(QPen(QBrush(gradient), 0));
    painter.drawText(bounds, Qt::AlignLeft | Qt::AlignVCenter, display);
}
} // namespace litecode::ui::components
