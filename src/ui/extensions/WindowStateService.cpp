#include "ui/extensions/WindowStateService.h"
#include "extensions/ExtensionManager.h"
#include <QApplication>
#include <QEvent>
#include <QWidget>

namespace litecode::ui {
WindowStateService::WindowStateService(extensions::ExtensionManager& manager, QWidget& window, QObject* parent)
    : QObject(parent), manager_(&manager), window_(&window) {
    qApp->installEventFilter(this);
    interval_.setParent(this);
    interval_.setObjectName("windowActivityIntervals");
    inactive_.setParent(this);
    inactive_.setObjectName("windowInactiveDebounce");
    inactive_.setInterval(10'000);
    inactive_.setSingleShot(true);
    connect(&inactive_, &QTimer::timeout, this, [this] {
        if (inputHeld_ || progressHeld_) return;
        state_.active = false;
        publish();
    });
    interval_.setInterval(30'000);
    connect(&interval_, &QTimer::timeout, this, [this] {
        if (++intervalsWithoutActivity_ == 2) {
            inputHeld_ = false;
            interval_.stop();
            scheduleInactive();
        }
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this] { refreshFocus(); });
    connect(&manager.progress(), &extensions::WorkbenchProgress::started, this,
        [this](const extensions::ProgressTaskInfo& task) {
            auto* delay = new QTimer(this);
            delay->setObjectName("windowProgressActivityDelay");
            delay->setSingleShot(true);
            delay->setInterval(15'000);
            progressLeases_[task.owner].insert(task.id, {delay, false});
            connect(delay, &QTimer::timeout, this, [this, owner = task.owner, id = task.id, delay] {
                auto tasks = progressLeases_.find(owner);
                if (tasks != progressLeases_.end()) {
                    auto lease = tasks->find(id);
                    // Upstream extendOnly prevents background progress from
                    // turning an inactive user into an active user.
                    if (lease != tasks->end() && !lease->held && state_.active) {
                        lease->held = true; ++progressHeld_; inactive_.stop();
                    }
                }
                delay->deleteLater();
            });
            delay->start();
        });
    connect(&manager.progress(), &extensions::WorkbenchProgress::ended, this,
        [this](const QString& owner, quint64 id) {
            auto tasks = progressLeases_.find(owner);
            if (tasks == progressLeases_.end()) return;
            const auto lease = tasks->take(id);
            if (lease.delay) { lease.delay->stop(); lease.delay->deleteLater(); }
            if (lease.held) --progressHeld_;
            if (tasks->isEmpty()) progressLeases_.erase(tasks);
            if (lease.held) scheduleInactive();
        });
    interval_.start();
    refreshFocus();
}
bool WindowStateService::belongsToWindow(QWidget* widget) const {
    // Parented top-level dialogs and popups are part of this workbench too.
    for (auto* parent = widget; parent; parent = parent->parentWidget())
        if (parent == window_) return true;
    return false;
}
void WindowStateService::refreshFocus() {
    state_.focused = qApp->applicationState() == Qt::ApplicationActive && belongsToWindow(qApp->activeWindow());
    publish();
}
void WindowStateService::activity() {
    intervalsWithoutActivity_ = 0;
    if (!inputHeld_) { inputHeld_ = true; interval_.start(); }
    inactive_.stop();
    if (state_.active) return;
    state_.active = true;
    publish();
}
void WindowStateService::scheduleInactive() {
    if (!inputHeld_ && !progressHeld_ && state_.active) inactive_.start();
}
void WindowStateService::publish() { if (manager_) manager_->setWindowState(state_); }
bool WindowStateService::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (belongsToWindow(widget)) {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
            activity(); break;
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
            QTimer::singleShot(0, this, [this] { refreshFocus(); }); break;
        default: break;
        }
    }
    return QObject::eventFilter(watched, event);
}
}
