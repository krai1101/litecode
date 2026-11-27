#include "ui/components/CircularProgress.h"
#include "ui/components/PopupSurface.h"
#include <QEvent>
#include <QApplication>
#include <QCursor>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
namespace litecode::ui::components {
CircularProgress::CircularProgress(QWidget* parent) : QProgressBar(parent) {
    setProperty("customTooltip", true);
    qApp->installEventFilter(this);
    setRange(0, 100);
    setTextVisible(false);
    setFixedSize(24, 24);
    tip_ = new QFrame(this, Qt::ToolTip | Qt::FramelessWindowHint);
    tip_->setObjectName("circularProgressTip");
    tip_->setAttribute(Qt::WA_TranslucentBackground);
    tip_->setAttribute(Qt::WA_ShowWithoutActivating);
    auto* layout = new QVBoxLayout(tip_);
    layout->setContentsMargins(0, 0, 0, 0);
    details_ = new QLabel(tip_);
    details_->setTextFormat(Qt::RichText);
    details_->setAlignment(Qt::AlignCenter);
    details_->setStyleSheet("background:#3b3b3b;color:white;border-radius:14px;"
                            "padding:8px 14px;font-size:12px;font-weight:400;");
    layout->addWidget(details_);
}
void CircularProgress::setDetails(const QString& text) {
    setAccessibleDescription(text);
    setToolTip(text);
    const auto lines = text.split('\n');
    QStringList escaped;
    for (const auto& line : lines)
        escaped.append(line.toHtmlEscaped());
    if (!escaped.isEmpty())
        escaped[0] = "<span style='color:#bbbbbb'>" + escaped[0] + "</span>";
    details_->setText(escaped.join("<br>"));
    if (tip_->isVisible())
        tip_->setGeometry(PopupSurface::anchoredGeometry(*this, tip_->sizeHint(), true, true));
}
void CircularProgress::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto color = palette().color(QPalette::WindowText);
    auto track = color;
    track.setAlphaF(.18);
    const QRectF ring(width() / 2.0 - 6, height() / 2.0 - 6, 12, 12);
    painter.setPen(QPen(track, 2));
    painter.drawEllipse(ring);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, 90 * 16, -qBound(0, value(), 100) * 360 * 16 / 100);
}
bool CircularProgress::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        tip_->setGeometry(PopupSurface::anchoredGeometry(*this, tip_->sizeHint(), true, true));
        tip_->show();
        event->accept();
        return true;
    }
    if (tip_ && (event->type() == QEvent::Hide ||
                 (event->type() == QEvent::Leave &&
                  !rect().contains(mapFromGlobal(QCursor::pos())))))
        tip_->hide();
    return QProgressBar::event(event);
}
bool CircularProgress::eventFilter(QObject* watched, QEvent* event) {
    if (tip_ && tip_->isVisible()) {
        // A native tooltip can generate Leave without moving the pointer.
        // Track the actual anchor bounds so showing it does not immediately hide it.
        const bool moved = event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove;
        if ((moved && !rect().contains(mapFromGlobal(QCursor::pos()))) ||
            (watched == window() && event->type() == QEvent::WindowDeactivate))
            tip_->hide();
    }
    return QProgressBar::eventFilter(watched, event);
}
} // namespace litecode::ui::components
