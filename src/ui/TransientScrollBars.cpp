#include "ui/TransientScrollBars.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>

namespace litecode::ui {
namespace {

constexpr auto kInstalledProperty = "litecodeTransientScrollBars";
constexpr auto kActiveProperty = "scrollbarActive";

} // namespace

void TransientScrollBars::install(QAbstractScrollArea* area, bool alwaysVisible) {
    if (area == nullptr || area->property(kInstalledProperty).toBool()) {
        return;
    }
    area->setProperty(kInstalledProperty, true);
    new TransientScrollBars(area, alwaysVisible);
}

void TransientScrollBars::installIn(QWidget* widget, bool alwaysVisible) {
    if (widget == nullptr) {
        return;
    }
    if (auto* area = qobject_cast<QAbstractScrollArea*>(widget)) {
        install(area, alwaysVisible);
    }
    for (QAbstractScrollArea* area : widget->findChildren<QAbstractScrollArea*>()) {
        install(area, alwaysVisible);
    }
}

TransientScrollBars::TransientScrollBars(QAbstractScrollArea* area, bool alwaysVisible)
    : QObject(area), area_(area), vertical_(area->verticalScrollBar()),
      horizontal_(area->horizontalScrollBar()), hideTimer_(new QTimer(this)),
      alwaysVisible_(alwaysVisible) {
    hideTimer_->setSingleShot(true);
    hideTimer_->setInterval(900);
    connect(hideTimer_, &QTimer::timeout, this, [this] {
        if (!pointerOverBar_) {
            setActive(false);
        }
    });

    for (QScrollBar* bar : {vertical_.data(), horizontal_.data()}) {
        if (bar == nullptr) {
            continue;
        }
        bar->setAttribute(Qt::WA_Hover, true);
        bar->installEventFilter(this);
        connect(bar, &QScrollBar::valueChanged, this, [this] {
            reveal();
            scheduleHide();
        });
    }
    if (area->viewport() != nullptr) {
        area->viewport()->installEventFilter(this);
    }
    setActive(alwaysVisible_);
}

bool TransientScrollBars::eventFilter(QObject* watched, QEvent* event) {
    const bool isBar = watched == vertical_ || watched == horizontal_;
    if (isBar && (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter)) {
        pointerOverBar_ = true;
        reveal();
    } else if (isBar && (event->type() == QEvent::Leave || event->type() == QEvent::HoverLeave)) {
        pointerOverBar_ = false;
        scheduleHide();
    } else if (watched == area_->viewport() && event->type() == QEvent::Wheel) {
        reveal();
        scheduleHide();
    }
    return QObject::eventFilter(watched, event);
}

void TransientScrollBars::reveal() {
    setActive(true);
    hideTimer_->stop();
}

void TransientScrollBars::scheduleHide() {
    if (!alwaysVisible_ && !pointerOverBar_) {
        hideTimer_->start();
    }
}

void TransientScrollBars::setActive(bool active) {
    for (QScrollBar* bar : {vertical_.data(), horizontal_.data()}) {
        if (bar == nullptr) {
            continue;
        }
        const QVariant current = bar->property(kActiveProperty);
        if (current.isValid() && current.toBool() == active) {
            continue;
        }
        bar->setProperty(kActiveProperty, active);
        refresh(bar);
    }
}

void TransientScrollBars::refresh(QScrollBar* bar) {
    bar->style()->unpolish(bar);
    bar->style()->polish(bar);
    bar->update();
}

} // namespace litecode::ui
