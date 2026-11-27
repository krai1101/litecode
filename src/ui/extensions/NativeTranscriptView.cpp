#include "ui/extensions/NativeTranscriptView.h"
#include "ui/components/IconLabelButton.h"
#include <QApplication>
#include <QScrollBar>
#include <QWheelEvent>
namespace litecode::ui {
bool NativeTranscriptView::forwardWheelAtEdge(QAbstractScrollArea* source, QWheelEvent* event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
        return false;
    const auto delta = event->pixelDelta().isNull() ? event->angleDelta() : event->pixelDelta();
    if (!delta.y() || qAbs(delta.x()) > qAbs(delta.y()))
        return false;
    const auto* bar = source->verticalScrollBar();
    if (source->verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff &&
        ((delta.y() > 0 && bar->value() > bar->minimum()) ||
         (delta.y() < 0 && bar->value() < bar->maximum())))
        return false;
    for (auto* ancestor = source->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto* transcript = qobject_cast<NativeTranscriptView*>(ancestor)) {
            QWheelEvent forwarded(transcript->viewport()->mapFromGlobal(event->globalPosition()),
                                  event->globalPosition(), event->pixelDelta(), event->angleDelta(),
                                  event->buttons(), event->modifiers(), event->phase(),
                                  event->inverted(), event->source(), event->pointingDevice());
            forwarded.setTimestamp(event->timestamp());
            transcript->wheelEvent(&forwarded);
            // The transcript owns the gesture even when it is already at an
            // edge: it can request another history page without moving yet.
            event->accept();
            return true;
        }
    }
    return false;
}
NativeTranscriptView::NativeTranscriptView(QWidget* parent) : QScrollArea(parent) {
    wheelMotion_.setDuration(90);
    wheelMotion_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&wheelMotion_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { verticalScrollBar()->setValue(value.toInt()); });
    connect(&wheelMotion_, &QVariantAnimation::finished, this,
            [this] { notifyNavigation(verticalScrollBar()->value(), wheelDirection_); });
    connect(verticalScrollBar(), &QScrollBar::sliderPressed, &wheelMotion_,
            &QVariantAnimation::stop);
    latest_ = new components::IconLabelButton(viewport());
    latest_->setObjectName("nativeTranscriptLatest");
    latest_->setIconLabel("$(arrow-down)");
    latest_->setFixedSize(32, 32);
    latest_->setAccessibleName(tr("Scroll to latest messages"));
    latest_->hide();
    follow_.setSingleShot(true);
    connect(&follow_, &QTimer::timeout, this, [this] {
        if (!widget())
            return;
        if (!anchorId_.isEmpty()) {
            if (auto* anchor = widget()->findChild<QWidget*>(anchorId_)) {
                verticalScrollBar()->setValue(anchor->mapTo(widget(), QPoint{}).y() -
                                              anchorOffset_);
            }
        } else if (following_)
            restorePosition(verticalScrollBar()->maximum(), true);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { updateNavigation(); });
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] {
        if (following_ || !anchorId_.isEmpty())
            follow_.start(0);
        updateNavigation();
    });
    connect(verticalScrollBar(), &QScrollBar::actionTriggered, this, [this](int action) {
        wheelMotion_.stop();
        const auto* bar = verticalScrollBar();
        int direction = bar->sliderPosition() - bar->value();
        // Wheel/page actions retain direction when already clamped at an edge.
        if (action == QAbstractSlider::SliderSingleStepSub ||
            action == QAbstractSlider::SliderPageStepSub ||
            action == QAbstractSlider::SliderToMinimum)
            direction = -1;
        else if (action == QAbstractSlider::SliderSingleStepAdd ||
                 action == QAbstractSlider::SliderPageStepAdd ||
                 action == QAbstractSlider::SliderToMaximum)
            direction = 1;
        notifyNavigation(bar->sliderPosition(), direction);
    });
    connect(latest_, &QToolButton::clicked, this, [this] {
        ++navigationEpoch_;
        restorePosition(verticalScrollBar()->maximum(), true);
        emit endRequested();
    });
}
void NativeTranscriptView::notifyNavigation(int position, int direction) {
    if (!direction)
        return;
    ++navigationEpoch_;
    anchorId_.clear();
    // Following is user intent. Layout/range changes must not re-enable it.
    following_ = direction > 0 && position >= verticalScrollBar()->maximum() - 2;
    auto* bar = verticalScrollBar();
    if (direction < 0 && position <= bar->minimum() + 32) {
        ++boundarySerial_;
        emit boundaryReached(true);
    } else if (direction > 0 && position >= bar->maximum() - 32) {
        ++boundarySerial_;
        emit boundaryReached(false);
    }
}
void NativeTranscriptView::restorePosition(int position, bool followEnd) {
    wheelMotion_.stop();
    follow_.stop();
    anchorId_.clear();
    following_ = followEnd;
    verticalScrollBar()->setValue(followEnd ? verticalScrollBar()->maximum() : position);
    updateNavigation();
}
void NativeTranscriptView::preserveAnchor(const QString& id, int offset) {
    anchorId_ = id;
    anchorOffset_ = offset;
    following_ = false;
    follow_.start(0);
}
void NativeTranscriptView::updateNavigation() {
    latest_->move((viewport()->width() - latest_->width()) / 2,
                  qMax(0, viewport()->height() - latest_->height() - 12));
    latest_->setVisible(verticalScrollBar()->maximum() > 0 &&
                        verticalScrollBar()->value() < verticalScrollBar()->maximum() - 32);
    latest_->raise();
}
void NativeTranscriptView::wheelEvent(QWheelEvent* event) {
    const int vertical =
        event->pixelDelta().y() ? event->pixelDelta().y() : event->angleDelta().y();
    if (vertical && verticalScrollBar()->maximum() == 0) {
        notifyNavigation(verticalScrollBar()->value(), -vertical);
        event->accept();
        return;
    }
    const bool unmodified = !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
    auto* bar = verticalScrollBar();
    if (unmodified && event->pixelDelta().y()) {
        // Trackpads already provide a continuous pixel stream. Do not add a
        // second animation or quantize it to mouse-wheel notches.
        wheelMotion_.stop();
        bar->setValue(bar->value() - event->pixelDelta().y());
        notifyNavigation(bar->value(), -event->pixelDelta().y());
        event->accept();
        return;
    }
    if (unmodified && event->phase() == Qt::NoScrollPhase && event->angleDelta().y() &&
        QApplication::wheelScrollLines() > 0) {
        const int direction = event->angleDelta().y() > 0 ? -1 : 1;
        const qreal origin =
            wheelMotion_.state() == QAbstractAnimation::Running && wheelDirection_ == direction
                ? wheelTarget_
                : bar->value();
        const qreal distance =
            qBound(-qreal(bar->pageStep()),
                   event->angleDelta().y() / 120.0 * QApplication::wheelScrollLines() *
                       qMax(1, bar->singleStep()),
                   qreal(bar->pageStep()));
        wheelTarget_ = qBound(qreal(bar->minimum()), origin - distance, qreal(bar->maximum()));
        if (qRound(wheelTarget_) != bar->value()) {
            ++navigationEpoch_;
            follow_.stop();
            anchorId_.clear();
            following_ = false;
            wheelMotion_.stop();
            wheelDirection_ = direction;
            wheelMotion_.setStartValue(bar->value());
            wheelMotion_.setEndValue(qRound(wheelTarget_));
            wheelMotion_.start();
            event->accept();
            return;
        }
    }
    wheelMotion_.stop();
    const auto serial = boundarySerial_;
    QScrollArea::wheelEvent(event);
    // Qt can emit a no-op slider action at a clamped edge. Only an actual
    // boundary notification suppresses the fallback, not the action itself.
    if (vertical && boundarySerial_ == serial)
        notifyNavigation(verticalScrollBar()->value(), -vertical);
}
void NativeTranscriptView::resizeEvent(QResizeEvent* event) {
    QScrollArea::resizeEvent(event);
    updateNavigation();
}
void NativeTranscriptView::showEvent(QShowEvent* event) {
    QScrollArea::showEvent(event);
    if (following_)
        follow_.start(0);
    updateNavigation();
}
} // namespace litecode::ui
