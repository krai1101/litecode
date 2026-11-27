#include "ui/components/ModalDialogQueue.h"
#include <QWidget>
#include <utility>

namespace litecode::ui {
ModalDialogQueue::ModalDialogQueue(QObject* parent) : QObject(parent) {
    setObjectName(QStringLiteral("workbenchModalDialogQueue"));
}
ModalDialogQueue& ModalDialogQueue::forWindow(QWidget& window) {
    if (auto* queue = window.findChild<ModalDialogQueue*>(QStringLiteral("workbenchModalDialogQueue"), Qt::FindDirectChildrenOnly)) return *queue;
    return *new ModalDialogQueue(&window);
}
void ModalDialogQueue::enqueue(QObject& service, const QString& key, std::function<void()> show) {
    if (!show) return;
    if (active_ && active_->identity == &service && active_->key == key) return;
    for (const auto& pending : pending_)
        if (pending.identity == &service && pending.key == key) return;
    if (!observed_.contains(&service)) {
        observed_.insert(&service);
        connect(&service, &QObject::destroyed, this, [this, identity = &service] { removeService(identity); });
    }
    pending_.enqueue({&service, &service, key, std::move(show)});
    schedule();
}
void ModalDialogQueue::release(QObject& service, const QString& key) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->identity == &service && it->key == key) it = pending_.erase(it);
        else ++it;
    }
    if (active_ && active_->identity == &service && active_->key == key) active_.reset();
    schedule();
}
void ModalDialogQueue::removeService(QObject* service) {
    observed_.remove(service);
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->identity == service) it = pending_.erase(it);
        else ++it;
    }
    if (active_ && active_->identity == service) active_.reset();
    schedule();
}
void ModalDialogQueue::schedule() {
    if (scheduled_ || active_ || pending_.isEmpty()) return;
    scheduled_ = true;
    QMetaObject::invokeMethod(this, [this] { scheduled_ = false; next(); }, Qt::QueuedConnection);
}
void ModalDialogQueue::next() {
    if (active_) return;
    while (!pending_.isEmpty()) {
        auto task = pending_.dequeue();
        if (!task.service) continue;
        active_ = std::move(task);
        // Copy before calling: completing synchronously can release active_.
        const auto show = active_->show;
        show();
        return;
    }
}
} // namespace litecode::ui
