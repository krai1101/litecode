#include "extensions/HostChatParticipant.h"
#include "extensions/IconWire.h"
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <utility>

namespace litecode::extensions {
struct HostChatParticipant::Runtime {
    mutable QMutex mutex;
    QString id;
    std::shared_ptr<ChatRequestHandler> handler;
    std::optional<IconPath> icon;
    std::shared_ptr<ChatFollowupProvider> followups;
    std::function<void()> unregister;
    MetadataChanged metadataChanged;
    QPointer<QObject> dispatcher;
    EventEmitter<ChatResultFeedback> feedback;
    bool disposed{}, metadataScheduled{};
};
HostChatParticipant::HostChatParticipant(QObject& dispatcher, QString id, ChatRequestHandler handler,
    std::function<void()> dispose, MetadataChanged metadataChanged) : runtime_(std::make_shared<Runtime>()) {
    if (!handler) throw std::invalid_argument("Invalid chat request handler.");
    runtime_->dispatcher = &dispatcher;
    runtime_->id = std::move(id); runtime_->handler = std::make_shared<ChatRequestHandler>(std::move(handler));
    runtime_->unregister = std::move(dispose); runtime_->metadataChanged = std::move(metadataChanged);
}
HostChatParticipant::~HostChatParticipant() { try { dispose(); } catch (...) {} }
QString HostChatParticipant::id() const { return runtime_->id; }
ChatRequestHandler HostChatParticipant::requestHandler() const {
    std::shared_ptr<ChatRequestHandler> handler;
    { QMutexLocker lock(&runtime_->mutex); handler = runtime_->handler; }
    // Copying a std::function can invoke extension-defined capture constructors.
    // Neither those constructors nor destructors may run under our state lock.
    return *handler;
}
void HostChatParticipant::requestHandler(ChatRequestHandler handler) {
    if (!handler) throw std::invalid_argument("Invalid chat request handler.");
    auto next = std::make_shared<ChatRequestHandler>(std::move(handler));
    std::shared_ptr<ChatRequestHandler> previous;
    { QMutexLocker lock(&runtime_->mutex); previous = std::exchange(runtime_->handler, std::move(next)); }
}
std::optional<IconPath> HostChatParticipant::iconPath() const {
    QMutexLocker lock(&runtime_->mutex); return runtime_->icon;
}
void HostChatParticipant::iconPath(std::optional<IconPath> value) {
    if (value && !wire::decodeIconPath(wire::encodeIconPath(*value)))
        throw std::invalid_argument("Invalid chat participant icon path.");
    const auto runtime = runtime_;
    { QMutexLocker lock(&runtime->mutex); runtime->icon = std::move(value); }
    scheduleMetadata(runtime);
}
std::shared_ptr<ChatFollowupProvider> HostChatParticipant::followupProvider() const {
    QMutexLocker lock(&runtime_->mutex); return runtime_->followups;
}
void HostChatParticipant::followupProvider(std::shared_ptr<ChatFollowupProvider> provider) {
    const auto runtime = runtime_;
    std::shared_ptr<ChatFollowupProvider> previous;
    { QMutexLocker lock(&runtime->mutex); previous = std::exchange(runtime->followups, std::move(provider)); }
    scheduleMetadata(runtime);
}
Event<ChatResultFeedback> HostChatParticipant::onDidReceiveFeedback() const { return runtime_->feedback.event(); }
void HostChatParticipant::fireFeedback(const ChatResultFeedback& feedback) {
    const auto runtime = runtime_;
    { QMutexLocker lock(&runtime->mutex); if (runtime->disposed) return; }
    runtime->feedback.fire(feedback);
}
void HostChatParticipant::scheduleMetadata(const std::shared_ptr<Runtime>& runtime) {
    QPointer<QObject> dispatcher;
    {
        QMutexLocker lock(&runtime->mutex);
        if (runtime->disposed || runtime->metadataScheduled || !runtime->dispatcher) return;
        runtime->metadataScheduled = true; dispatcher = runtime->dispatcher;
    }
    QMetaObject::invokeMethod(dispatcher, [weak = std::weak_ptr<Runtime>(runtime)] {
        const auto state = weak.lock(); if (!state) return;
        MetadataChanged changed; std::optional<IconPath> icon; bool hasFollowups{};
        {
            QMutexLocker lock(&state->mutex);
            state->metadataScheduled = false;
            if (state->disposed) return;
            changed = state->metadataChanged; icon = state->icon; hasFollowups = bool(state->followups);
        }
        if (changed) changed(icon ? wire::encodeIconPath(*icon) : QJsonValue(QJsonValue::Undefined), hasFollowups);
    }, Qt::QueuedConnection);
}
void HostChatParticipant::dispose() {
    const auto runtime = runtime_;
    std::function<void()> unregister; MetadataChanged metadataChanged;
    std::shared_ptr<ChatFollowupProvider> previous;
    QPointer<QObject> dispatcher;
    {
        QMutexLocker lock(&runtime->mutex);
        if (runtime->disposed) return;
        runtime->disposed = true;
        unregister = std::exchange(runtime->unregister, {});
        metadataChanged = std::exchange(runtime->metadataChanged, {});
        previous = std::exchange(runtime->followups, {});
        dispatcher = runtime->dispatcher;
    }
    runtime->feedback.dispose();
    // Captured handlers/icons remain readable on a retained disposed object,
    // matching the upstream property facade. Registration is still removed.
    if (!unregister || !dispatcher) return;
    if (QThread::currentThread() == dispatcher->thread()) unregister();
    else QMetaObject::invokeMethod(dispatcher, std::move(unregister), Qt::QueuedConnection);
}
}
