#include "ui/extensions/StatusProgressService.h"
#include "ui/components/IconLabelButton.h"
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>

namespace litecode::ui {
namespace {
QString key(const QString& owner, quint64 id) { return owner + QChar(0) + QString::number(id); }
QString text(const extensions::ProgressTaskInfo& task) {
    const auto title = task.options.title.value_or(QString());
    const auto message = task.message.value_or(QString());
    return title.isEmpty() ? message : message.isEmpty() ? title : title + ": " + message;
}
} // namespace
StatusProgressService::StatusProgressService(extensions::WorkbenchProgress& progress,
                                             QStatusBar& bar, QObject* parent)
    : QObject(parent), timer_(new QTimer(this)) {
    clock_.start();
    button_ = new components::IconLabelButton(&bar);
    button_->setObjectName(QStringLiteral("extensionProgressStatus"));
    button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button_->setIconSize(QSize(14, 14));
    bar.addPermanentWidget(button_);
    button_->hide();
    connect(button_.data(), &QToolButton::clicked, this, [this] {
        const auto task = tasks_.constFind(selected_);
        if (task != tasks_.cend() && !task->ended)
            emit revealRequested(task->info.owner, task->info.id);
    });
    timer_->setInterval(40);
    connect(timer_, &QTimer::timeout, this, &StatusProgressService::refresh);
    connect(&progress, &extensions::WorkbenchProgress::started, this,
            [this](const extensions::ProgressTaskInfo& info) {
                const auto* location =
                    std::get_if<extensions::ProgressLocation>(&info.options.location);
                if (!location || (*location != extensions::ProgressLocation::Window &&
                                  *location != extensions::ProgressLocation::Notification))
                    return;
                Task task;
                task.info = info;
                task.eligibleAt = clock_.elapsed() + 150;
                task.order = ++nextOrder_;
                task.notificationVisible = *location == extensions::ProgressLocation::Notification;
                tasks_.insert(key(info.owner, info.id), task);
                if (!timer_->isActive())
                    timer_->start();
            });
    connect(&progress, &extensions::WorkbenchProgress::changed, this,
            [this](const extensions::ProgressTaskInfo& info) {
                auto task = tasks_.find(key(info.owner, info.id));
                if (task != tasks_.end() && !task->ended)
                    task->info = info;
                refresh();
            });
    connect(&progress, &extensions::WorkbenchProgress::ended, this,
            [this](const QString& owner, quint64 id) {
                auto task = tasks_.find(key(owner, id));
                if (task != tasks_.end())
                    task->ended = true;
                refresh();
            });
    connect(&progress, &QObject::destroyed, this, [this] {
        tasks_.clear();
        refresh();
    });
}
StatusProgressService::~StatusProgressService() { delete button_.data(); }
void StatusProgressService::setNotificationVisible(const QString& owner, quint64 id, bool visible) {
    auto task = tasks_.find(key(owner, id));
    if (task == tasks_.end())
        return;
    task->notificationVisible = visible;
    if (!visible)
        task->eligibleAt = clock_.elapsed() + 150;
    refresh();
}
void StatusProgressService::refresh() {
    if (!button_) {
        timer_->stop();
        return;
    }
    const auto now = clock_.elapsed();
    quint64 order = 0;
    QString chosen;
    for (auto it = tasks_.begin(); it != tasks_.end();) {
        if (it->info.cancellationRequested ||
            (it->ended && (it->shownAt < 0 || now >= it->shownAt + 150))) {
            it = tasks_.erase(it);
            continue;
        }
        if (!it->notificationVisible && now >= it->eligibleAt && !text(it->info).isEmpty() &&
            it->order > order) {
            order = it->order;
            chosen = it.key();
        }
        ++it;
    }
    selected_ = chosen;
    if (chosen.isEmpty())
        button_->hide();
    else {
        auto& task = tasks_[chosen];
        if (task.shownAt < 0)
            task.shownAt = now;
        const auto label = text(task.info);
        static_cast<components::IconLabelButton*>(button_.data())
            ->setIconLabel(QStringLiteral("$(loading~spin) ") + label);
        const auto tooltip =
            task.info.source.isEmpty() ? label : "[" + task.info.source + "] " + label;
        button_->setToolTip(components::IconLabelButton::stripIcons(tooltip).trimmed());
        button_->setEnabled(!task.ended);
        button_->show();
    }
    bool needsTimer = false;
    for (const auto& task : tasks_)
        if (!task.notificationVisible || task.ended) {
            needsTimer = true;
            break;
        }
    if (!needsTimer)
        timer_->stop();
    else if (!timer_->isActive())
        timer_->start();
}
} // namespace litecode::ui
