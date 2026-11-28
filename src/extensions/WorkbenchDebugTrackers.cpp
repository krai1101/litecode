#include "extensions/WorkbenchDebugTrackers.h"
#include <QTimer>

namespace litecode::extensions {
WorkbenchDebugTrackers::WorkbenchDebugTrackers(
    std::function<void(const QString&, const QJsonObject&)> send, QObject* parent)
    : QObject(parent), send_(std::move(send)) {}
bool WorkbenchDebugTrackers::registerFactory(const QString& owner, const QJsonObject& message) {
    bool valid{};
    const auto text = message.value("handle").toString();
    const auto handle = text.toULongLong(&valid);
    const auto type = message.value("type").toString();
    if (!valid || !handle || QString::number(handle) != text || type.isEmpty() ||
        type.contains(QChar('\0')) || factories_.value(owner).contains(handle))
        return false;
    factories_[owner].insert(handle, type);
    return true;
}
bool WorkbenchDebugTrackers::unregisterFactory(const QString& owner, quint64 handle) {
    auto found = factories_.find(owner);
    return found != factories_.end() && found->remove(handle);
}
void WorkbenchDebugTrackers::removeOwner(const QString& owner) {
    factories_.remove(owner);
    const auto ids = groups_.keys();
    for (const auto id : ids) {
        auto found = groups_.find(id);
        if (found == groups_.end())
            continue;
        found->owners.remove(owner);
        found->waiting.remove(owner);
        const auto group = found.value();
        if (group.adapterOwner == owner) {
            if (!group.started) {
                groups_.erase(found);
                cancelOwners(id, group.owners);
                if (!group.ready)
                    emit prepared(id, {}, "Debug adapter extension host stopped.");
            }
            emit adapterOwnerUnavailable(group.session.value("id").toString());
        } else
            finish(id);
    }
}
void WorkbenchDebugTrackers::prepare(quint64 id, const QString& adapterOwner,
                                     const QJsonObject& session, const QJsonObject& descriptor) {
    if (!id || groups_.contains(id))
        return;
    Group group;
    group.session = session;
    group.descriptor = descriptor;
    group.adapterOwner = adapterOwner;
    const auto type = session.value("type").toString();
    for (auto owner = factories_.begin(); owner != factories_.end(); ++owner)
        for (const auto& factoryType : owner.value())
            if (factoryType == type || factoryType == "*") {
                group.owners.insert(owner.key());
                break;
            }
    group.waiting = group.owners;
    groups_.insert(id, group);
    for (const auto& owner : group.owners)
        send_(owner, {{"command", "prepareDebugTrackers"},
                      {"requestId", QString::number(id)},
                      {"session", session}});
    // Host factories get the upstream one-second budget. This boundary adds
    // a small IPC grace period, not an unbounded extension-controlled delay.
    QTimer::singleShot(1200, this, [this, id] { finish(id, true); });
    QTimer::singleShot(0, this, [this, id] { finish(id); });
}
bool WorkbenchDebugTrackers::acceptReady(const QString& owner, quint64 id) {
    auto found = groups_.find(id);
    if (found == groups_.end() || found->ready)
        return true;
    if (!found->owners.contains(owner))
        return false;
    found->waiting.remove(owner);
    finish(id);
    return true;
}
void WorkbenchDebugTrackers::finish(quint64 id, bool timeout) {
    auto found = groups_.find(id);
    if (found == groups_.end() || found->ready || (!timeout && !found->waiting.isEmpty()))
        return;
    auto abandoned = QSet<QString>{};
    if (timeout && !found->waiting.isEmpty()) {
        abandoned = found->owners;
        found->owners.clear();
    }
    found->waiting.clear();
    found->ready = true;
    const auto descriptor = found->descriptor;
    cancelOwners(id, abandoned);
    emit prepared(id, descriptor, {});
}
void WorkbenchDebugTrackers::cancelOwners(quint64 id, const QSet<QString>& owners) {
    for (const auto& owner : owners)
        send_(owner, {{"command", "releaseDebugTrackers"}, {"requestId", QString::number(id)}});
}
bool WorkbenchDebugTrackers::release(quint64 id) {
    const auto found = groups_.find(id);
    if (found == groups_.end() || found->started)
        return false;
    const auto group = found.value();
    groups_.erase(found);
    cancelOwners(id, group.owners);
    if (!group.ready)
        emit prepared(id, {}, "Debug adapter resolution was canceled.");
    return true;
}
void WorkbenchDebugTrackers::publish(const QString& sessionId, const QJsonObject& event) {
    for (auto it = groups_.begin(); it != groups_.end(); ++it) {
        if (it->session.value("id").toString() != sessionId || !it->ready)
            continue;
        const auto id = it.key();
        const auto owners = it->owners;
        const auto kind = event.value("kind").toString();
        if (kind == "start")
            it->started = true;
        if (kind == "exit")
            groups_.erase(it);
        QJsonObject message = event;
        message.insert("command", "debugTrackerEvent");
        message.insert("requestId", QString::number(id));
        for (const auto& owner : owners)
            send_(owner, message);
        return;
    }
}
} // namespace litecode::extensions
