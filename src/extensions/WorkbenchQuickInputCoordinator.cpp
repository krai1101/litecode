#include "extensions/WorkbenchQuickInputCoordinator.h"
#include <utility>
namespace litecode::extensions {
WorkbenchQuickInputCoordinator::WorkbenchQuickInputCoordinator(WorkbenchInputBox& boxes, WorkbenchQuickPick& picks, QObject* parent)
    : QObject(parent), boxes_(&boxes), picks_(&picks) {
    connect(&boxes, &WorkbenchInputBox::requested, this, [this](const InputBoxRequest& request) {
        replace({false, request.owner, request.id});
    });
    connect(&picks, &WorkbenchQuickPick::requested, this, [this](const QuickPickRequest& request) {
        replace({true, request.owner, request.id});
    });
    connect(&boxes, &WorkbenchInputBox::dismissed, this, [this](const QString& owner, quint64 id) { clear(false, owner, id); });
    connect(&picks, &WorkbenchQuickPick::dismissed, this, [this](const QString& owner, quint64 id) { clear(true, owner, id); });
}
void WorkbenchQuickInputCoordinator::replace(Active next) {
    const auto previous = std::exchange(active_, std::move(next));
    if (!previous) return;
    // Commit the replacement before completing the old request: dismissal may reenter.
    if (previous->picker) { if (picks_) picks_->complete(previous->owner, previous->id, {}); }
    else if (boxes_) boxes_->complete(previous->owner, previous->id, {});
}
void WorkbenchQuickInputCoordinator::clear(bool picker, const QString& owner, quint64 id) {
    if (active_ && active_->picker == picker && active_->owner == owner && active_->id == id) active_.reset();
}
}
