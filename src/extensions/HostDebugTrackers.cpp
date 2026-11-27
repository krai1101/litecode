#include "extensions/HostDebugTrackers.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace litecode::extensions {
HostDebugTrackers::HostDebugTrackers(std::function<void(const QJsonObject&)> send, QObject* parent)
    : QObject(parent), send_(std::move(send)) {}
HostDebugTrackers::~HostDebugTrackers() { shutdown(); }
Disposable HostDebugTrackers::registerDebugAdapterTrackerFactory(
    const QString& type, std::shared_ptr<DebugAdapterTrackerFactory> factory) {
    if (QThread::currentThread() != thread())
        throw std::logic_error("Debug trackers must use the host dispatcher.");
    if (stopped_ || !factory || type.isEmpty() || type.contains(QChar('\0')))
        throw std::invalid_argument("Invalid debug adapter tracker factory.");
    if (nextHandle_ == std::numeric_limits<quint64>::max())
        throw std::overflow_error("Debug tracker IDs exhausted.");
    const auto handle = ++nextHandle_;
    factories_.insert(handle, {type, std::move(factory)});
    send_({{"event", "registeredDebugAdapterTrackerFactory"},
           {"handle", QString::number(handle)},
           {"type", type}});
    const QPointer<HostDebugTrackers> self(this);
    return Disposable([self, handle] {
        if (!self)
            return;
        const auto remove = [self, handle] {
            if (!self || !self->factories_.remove(handle))
                return;
            if (!self->stopped_)
                self->send_({{"event", "unregisteredDebugAdapterTrackerFactory"},
                             {"handle", QString::number(handle)}});
            // Registration disposal affects future sessions, not an existing tracker.
        };
        if (QThread::currentThread() == self->thread())
            remove();
        else
            QMetaObject::invokeMethod(self, remove, Qt::QueuedConnection);
    });
}
void HostDebugTrackers::prepare(quint64 id, std::shared_ptr<DebugSession> session) {
    if (stopped_ || !id || !session || groups_.contains(id))
        return;
    const auto factories = factories_;
    Group group;
    for (auto it = factories.begin(); it != factories.end(); ++it)
        if (it->type == session->type() || it->type == "*")
            group.awaiting.insert(it.key());
    groups_.insert(id, group);
    auto handles = group.awaiting.values();
    std::sort(handles.begin(), handles.end());
    for (const auto handle : handles) {
        if (stopped_ || !groups_.contains(id))
            break;
        const auto factory = factories.value(handle).provider;
        try {
            auto result = factory->createDebugAdapterTracker(session);
            std::visit(
                [this, id, handle, factory](auto&& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::shared_ptr<DebugAdapterTracker>>)
                        complete(id, handle, std::move(value));
                    else if constexpr (std::is_same_v<
                                           T, std::optional<std::shared_ptr<DebugAdapterTracker>>>)
                        complete(id, handle, value.value_or(nullptr));
                    else
                        watch(id, handle, value, factory);
                },
                std::move(result));
        } catch (...) {
            complete(id, handle, {});
        } // Observer creation must not break debugging.
    }
    ready(id);
    QTimer::singleShot(1000, this, [this, id] { ready(id, true); });
}
template <typename T>
void HostDebugTrackers::watch(quint64 id, quint64 handle, const QFuture<T>& future,
                              std::shared_ptr<DebugAdapterTrackerFactory> factory) {
    auto* watcher = new QFutureWatcher<T>(this);
    connect(watcher, &QFutureWatcher<T>::finished, this,
            [this, watcher, id, handle, factory = std::move(factory)] {
                std::shared_ptr<DebugAdapterTracker> tracker;
                try {
                    auto future = watcher->future();
                    future.waitForFinished();
                    if (!future.isCanceled() && future.resultCount()) {
                        if constexpr (std::is_same_v<T, std::shared_ptr<DebugAdapterTracker>>)
                            tracker = future.result();
                        else
                            tracker = future.result().value_or(nullptr);
                    }
                } catch (...) {
                }
                complete(id, handle, std::move(tracker));
                watcher->deleteLater();
            });
    watcher->setFuture(future);
}
void HostDebugTrackers::complete(quint64 id, quint64 handle,
                                 std::shared_ptr<DebugAdapterTracker> tracker) {
    auto found = groups_.find(id);
    if (found == groups_.end() || found->ready || !found->awaiting.remove(handle))
        return;
    if (tracker)
        found->trackers.insert(handle, std::move(tracker));
    ready(id);
}
void HostDebugTrackers::ready(quint64 id, bool timeout) {
    auto found = groups_.find(id);
    if (found == groups_.end() || found->ready || (!timeout && !found->awaiting.isEmpty()))
        return;
    if (timeout && !found->awaiting.isEmpty())
        found->trackers.clear(); // Upstream's aggregate timeout discards the group.
    found->awaiting.clear();
    found->ready = true;
    send_({{"event", "debugTrackersReady"}, {"requestId", QString::number(id)}});
}
bool HostDebugTrackers::event(quint64 id, const QJsonObject& message) {
    auto found = groups_.find(id);
    if (found == groups_.end())
        return true; // Canceled collection or closed session.
    if (!found->ready)
        return false;
    const auto kind = message.value("kind").toString();
    if (kind != "start" && kind != "receive" && kind != "send" && kind != "stop" &&
        kind != "error" && kind != "exit")
        return false;
    if ((kind == "receive" || kind == "send") && !message.value("message").isObject())
        return false;
    if (kind == "error" && !message.value("error").isString())
        return false;
    std::optional<int> code;
    std::optional<QString> signal;
    if (kind == "exit") {
        const auto value = message.value("code");
        if (!value.isUndefined() && !value.isNull()) {
            if (!value.isDouble() || value.toDouble() != value.toInt())
                return false;
            code = value.toInt();
        }
        if (message.contains("signal")) {
            if (!message.value("signal").isString())
                return false;
            signal = message.value("signal").toString();
        }
    }
    if (kind == "start") {
        if (found->started)
            return true;
        found->started = true;
    }
    if (kind == "stop") {
        if (found->stopping)
            return true;
        found->stopping = true;
    }
    const auto trackers = found->trackers;
    if (kind == "exit")
        groups_.erase(found);
    for (const auto& tracker : trackers) {
        try {
            if (kind == "start")
                tracker->onWillStartSession();
            else if (kind == "receive")
                tracker->onWillReceiveMessage(message.value("message").toObject());
            else if (kind == "send")
                tracker->onDidSendMessage(message.value("message").toObject());
            else if (kind == "stop")
                tracker->onWillStopSession();
            else if (kind == "exit")
                tracker->onExit(code, signal);
            else {
                const std::runtime_error error(message.value("error").toString().toStdString());
                tracker->onError(error);
            }
        } catch (...) {
            send_({{"event", "debugTrackerDiagnostic"},
                   {"message", "A debug adapter tracker callback failed."}});
        }
    }
    return true;
}
void HostDebugTrackers::release(quint64 id) { groups_.remove(id); }
void HostDebugTrackers::shutdown() {
    if (stopped_)
        return;
    stopped_ = true;
    groups_.clear();
    factories_.clear();
}
} // namespace litecode::extensions
