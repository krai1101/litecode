#include "extensions/WorkbenchProgress.h"
#include "extensions/ProgressWire.h"
#include <cmath>

namespace litecode::extensions {
bool WorkbenchProgress::accept(const QString& owner, const QString& source,
                               const QJsonObject& message) {
    bool valid = false;
    const auto encoded = message.value("id").toString();
    const auto id = encoded.toULongLong(&valid);
    if (owner.isEmpty() || !valid || !id || QString::number(id) != encoded ||
        !message.value("operation").isString())
        return false;
    const auto operation = message.value("operation").toString();
    if (operation == "start") {
        if (!message.value("options").isObject() || id <= lastStarted_.value(owner))
            return false;
        const auto options = wire::decodeProgressOptions(message.value("options").toObject());
        if (!options)
            return false;
        ProgressTaskInfo task{owner, source, id, *options, {}, {}, false};
        lastStarted_.insert(owner, id);
        tasks_[owner].insert(id, task);
        emit started(task);
        return true;
    }
    std::optional<ProgressReport> report;
    if (operation == "report") {
        if (!message.value("report").isObject())
            return false;
        report = wire::decodeProgressReport(message.value("report").toObject());
        if (!report)
            return false;
    } else if (operation != "end")
        return false;
    auto tasks = tasks_.find(owner);
    if (tasks == tasks_.end())
        return true; // Late reports/end are ignored upstream.
    auto task = tasks->find(id);
    if (task == tasks->end())
        return true;
    if (operation == "end") {
        tasks->erase(task);
        if (tasks->isEmpty())
            tasks_.erase(tasks);
        emit ended(owner, id);
    } else {
        if (report->increment) {
            const auto total = task->total.value_or(0) + *report->increment;
            if (!std::isfinite(total))
                return false;
            task->total = total;
        }
        if (report->message)
            task->message = report->message;
        const auto snapshot = *task;
        emit changed(snapshot);
    }
    return true;
}
bool WorkbenchProgress::cancel(const QString& owner, quint64 id) {
    auto tasks = tasks_.find(owner);
    if (tasks == tasks_.end())
        return false;
    auto task = tasks->find(id);
    if (task == tasks->end() || task->cancellationRequested ||
        !task->options.cancellable.value_or(false))
        return false;
    const auto* location = std::get_if<ProgressLocation>(&task->options.location);
    if (!location || *location != ProgressLocation::Notification)
        return false;
    task->cancellationRequested = true;
    const auto snapshot = *task;
    emit changed(snapshot);
    // Observers may synchronously finish/remove the task. Do not publish a
    // stale update after cancellation, or route cancellation to a removed host.
    if (this->task(owner, id))
        emit cancelRequested(owner, id);
    return true;
}
void WorkbenchProgress::removeOwner(const QString& owner) {
    lastStarted_.remove(owner);
    const auto tasks = tasks_.take(owner);
    for (const auto& task : tasks)
        emit ended(owner, task.id);
}
std::optional<ProgressTaskInfo> WorkbenchProgress::task(const QString& owner, quint64 id) const {
    const auto tasks = tasks_.constFind(owner);
    if (tasks == tasks_.cend())
        return {};
    const auto task = tasks->constFind(id);
    if (task == tasks->cend())
        return {};
    return *task;
}
} // namespace litecode::extensions
