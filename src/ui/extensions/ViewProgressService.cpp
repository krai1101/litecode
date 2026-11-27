#include "ui/extensions/ViewProgressService.h"
#include <QEvent>
#include <QProgressBar>
#include <QWidget>
#include <utility>

namespace litecode::ui {
namespace {
QString key(const QString& owner, quint64 id) { return owner + QChar(0) + QString::number(id); }
QProgressBar* indicator(QWidget& parent) {
    auto* result = new QProgressBar(&parent);
    result->setObjectName(QStringLiteral("extensionViewProgress"));
    result->setRange(0, 0);
    result->setTextVisible(false);
    result->setFocusPolicy(Qt::NoFocus);
    result->setAttribute(Qt::WA_TransparentForMouseEvents);
    result->setAccessibleName(ViewProgressService::tr("Operation in progress"));
    result->hide();
    return result;
}
}
ViewProgressService::ViewProgressService(extensions::WorkbenchProgress& progress, QObject* parent) : QObject(parent) {
    connect(&progress, &extensions::WorkbenchProgress::started, this, [this](const extensions::ProgressTaskInfo& task) {
        QString location;
        if (const auto* view = std::get_if<extensions::ProgressOptions::ViewLocation>(&task.options.location)) location = view->viewId;
        else if (std::get<extensions::ProgressLocation>(task.options.location) == extensions::ProgressLocation::SourceControl) location = "workbench.scm";
        else return;
        const auto taskKey = key(task.owner, task.id);
        taskViews_.insert(taskKey, location);
        auto view = views_.find(location);
        if (view == views_.end()) return;
        view->tasks.insert(taskKey);
        position(*view);
    });
    connect(&progress, &extensions::WorkbenchProgress::ended, this, [this](const QString& owner, quint64 id) {
        const auto taskKey = key(owner, id);
        const auto location = taskViews_.take(taskKey);
        auto view = views_.find(location);
        if (view != views_.end()) { view->tasks.remove(taskKey); position(*view); }
    });
    connect(&progress, &QObject::destroyed, this, &ViewProgressService::clear);
}
ViewProgressService::~ViewProgressService() { clear(); }
void ViewProgressService::clear() {
    const auto views = std::exchange(views_, {});
    for (const auto& view : views) {
        if (view.body) view.body->removeEventFilter(this);
        if (view.activity) view.activity->removeEventFilter(this);
        delete view.bar.data();
        delete view.badge.data();
    }
    taskViews_.clear();
}
void ViewProgressService::registerView(const QString& id, QWidget& body, QWidget* activity) {
    auto old = views_.take(id);
    if (old.body) old.body->removeEventFilter(this);
    if (old.activity) old.activity->removeEventFilter(this);
    delete old.bar.data();
    delete old.badge.data();
    View view;
    view.body = &body;
    view.activity = activity;
    view.bar = indicator(body);
    if (activity) view.badge = indicator(*activity);
    for (auto it = taskViews_.cbegin(); it != taskViews_.cend(); ++it) if (it.value() == id) view.tasks.insert(it.key());
    body.installEventFilter(this);
    if (activity) activity->installEventFilter(this);
    views_.insert(id, view);
    position(views_[id]);
}
void ViewProgressService::position(View& view) {
    const bool active = !view.tasks.isEmpty();
    if (view.bar && view.body) {
        view.bar->setGeometry(0, 0, view.body->width(), 2);
        view.bar->setVisible(active);
        if (active) view.bar->raise();
    }
    if (view.badge && view.activity) {
        const auto width = qMin(16, view.activity->width());
        view.badge->setGeometry(qMax(0, view.activity->width() - width - 4), qMax(0, view.activity->height() - 6), width, 2);
        view.badge->setVisible(active);
        if (active) view.badge->raise();
    }
}
bool ViewProgressService::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
        for (auto& view : views_) if (watched == view.body || watched == view.activity) position(view);
    }
    return QObject::eventFilter(watched, event);
}
} // namespace litecode::ui
