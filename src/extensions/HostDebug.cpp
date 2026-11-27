#include "extensions/HostDebug.h"
#include "extensions/DebugDescriptorWire.h"
#include "extensions/HostDebugConfigurations.h"
#include "extensions/HostDebugSessions.h"
#include "extensions/HostDebugStarts.h"
#include "extensions/HostDebugTrackers.h"
#include "extensions/HostInlineDebugAdapter.h"
#include <QFutureWatcher>
#include <QThread>
#include <limits>
#include <stdexcept>

namespace litecode::extensions {
namespace {
QString failure() {
    try {
        throw;
    } catch (const std::exception& error) {
        return QString::fromUtf8(error.what());
    } catch (...) {
        return "Debug adapter descriptor factory failed.";
    }
}
void disposeUnused(const std::optional<DebugAdapterDescriptor>& descriptor) noexcept {
    if (!descriptor)
        return;
    if (const auto* adapter = std::get_if<DebugAdapterInlineImplementation>(&*descriptor))
        if (adapter->implementation) {
            try {
                adapter->implementation->dispose();
            } catch (...) {
            }
        }
}
} // namespace
HostDebug::HostDebug(std::function<void(const QJsonObject&)> send)
    : send_(std::move(send)),
      configurations_(std::make_unique<HostDebugConfigurations>(send_, this)),
      sessions_(std::make_unique<HostDebugSessions>(send_, this)),
      trackers_(std::make_unique<HostDebugTrackers>(send_, this)),
      starts_(std::make_unique<HostDebugStarts>(send_, this)) {}
HostDebug::~HostDebug() { shutdown(); }
Disposable
HostDebug::registerDebugConfigurationProvider(const QString& type,
                                              std::shared_ptr<DebugConfigurationProvider> provider,
                                              DebugConfigurationProviderTriggerKind trigger) {
    return configurations_->registerDebugConfigurationProvider(type, std::move(provider), trigger);
}
HostDebugConfigurations& HostDebug::configurations() { return *configurations_; }
HostDebugSessions& HostDebug::sessions() { return *sessions_; }
HostDebugTrackers& HostDebug::trackers() { return *trackers_; }
HostDebugStarts& HostDebug::starts() { return *starts_; }
QFuture<bool> HostDebug::startDebugging(
    std::optional<WorkspaceFolder> folder, const std::variant<QString, QJsonObject>& configuration,
    const std::variant<std::shared_ptr<DebugSession>, DebugSessionOptions>& options) {
    return starts_->start(std::move(folder), configuration, options);
}
Disposable
HostDebug::registerDebugAdapterTrackerFactory(const QString& type,
                                              std::shared_ptr<DebugAdapterTrackerFactory> factory) {
    return trackers_->registerDebugAdapterTrackerFactory(type, std::move(factory));
}
std::shared_ptr<DebugSession> HostDebug::activeDebugSession() const { return sessions_->active(); }
Event<std::shared_ptr<DebugSession>> HostDebug::onDidStartDebugSession() const {
    return sessions_->started();
}
Event<std::shared_ptr<DebugSession>> HostDebug::onDidTerminateDebugSession() const {
    return sessions_->terminated();
}
Event<std::shared_ptr<DebugSession>> HostDebug::onDidChangeActiveDebugSession() const {
    return sessions_->activeChanged();
}
Event<DebugSessionCustomEvent> HostDebug::onDidReceiveDebugSessionCustomEvent() const {
    return sessions_->customEvent();
}
QFuture<void> HostDebug::stopDebugging(std::shared_ptr<DebugSession> session) {
    if (!session)
        session = sessions_->active();
    if (!session) {
        QPromise<void> promise;
        promise.start();
        auto result = promise.future();
        promise.finish();
        return result;
    }
    return sessions_->request(session->id(), "stop", {}, QJsonValue::Undefined)
        .then([](const QJsonValue&) {});
}
Disposable HostDebug::registerDebugAdapterDescriptorFactory(
    const QString& type, std::shared_ptr<DebugAdapterDescriptorFactory> provider) {
    if (QThread::currentThread() != thread())
        throw std::logic_error("Debug registration must run on the host dispatcher.");
    if (stopped_ || !provider || type.trimmed().isEmpty() || type.contains(QChar('\0')))
        throw std::invalid_argument("Invalid debug adapter descriptor factory registration.");
    if (factories_.contains(type))
        throw std::invalid_argument(
            "A debug adapter descriptor factory is already registered for this type.");
    if (nextToken_ == std::numeric_limits<quint64>::max())
        throw std::overflow_error("Debug registration IDs exhausted.");
    const auto token = ++nextToken_;
    factories_.insert(type, {std::move(provider), token});
    send_({{"event", "registeredDebugAdapterDescriptorFactory"}, {"type", type}});
    const QPointer<HostDebug> self(this);
    return Disposable([self, type, token] {
        if (!self)
            return;
        if (QThread::currentThread() == self->thread())
            self->remove(type, token);
        else
            QMetaObject::invokeMethod(
                self,
                [self, type, token] {
                    if (self)
                        self->remove(type, token);
                },
                Qt::QueuedConnection);
    });
}
void HostDebug::remove(const QString& type, quint64 token) {
    const auto found = factories_.constFind(type);
    if (found == factories_.cend() || found->token != token)
        return;
    factories_.remove(type);
    const auto pending = pending_;
    for (auto it = pending.begin(); it != pending.end(); ++it)
        if (it->type == type && it->token == token)
            complete(it.key(), {}, "Debug adapter descriptor factory was disposed.");
    if (!stopped_)
        send_({{"event", "unregisteredDebugAdapterDescriptorFactory"}, {"type", type}});
}
void HostDebug::resolve(quint64 id, const QJsonObject& context,
                        const std::optional<DebugAdapterExecutable>& executable) {
    if (stopped_ || !id || pending_.contains(id))
        return;
    try {
        auto session = sessions_->session(context);
        const auto type = session->type();
        const auto factory = factories_.value(type);
        pending_.insert(id, {type, factory.token});
        // Upstream debugServer takes precedence over both the provider and the
        // package executable. Do not invoke extension code for this override.
        const auto override = session->configuration().value("debugServer");
        if (override.isDouble()) {
            if (override.toDouble() != override.toInt(-1))
                throw std::invalid_argument("Invalid debugServer port.");
            complete(id, DebugAdapterServer{override.toInt(), {}}, {});
            return;
        }
        if (!factory.provider) {
            complete(id,
                     executable ? std::optional<DebugAdapterDescriptor>(*executable) : std::nullopt,
                     {});
            return;
        }
        auto result = factory.provider->createDebugAdapterDescriptor(session, executable);
        std::visit(
            [this, id, provider = factory.provider](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, DebugAdapterDescriptor>)
                    complete(id, value, {});
                else if constexpr (std::is_same_v<T, std::optional<DebugAdapterDescriptor>>)
                    complete(id, value, {});
                else
                    watch(id, value, provider);
            },
            std::move(result));
    } catch (...) {
        if (!pending_.contains(id))
            pending_.insert(id, {});
        complete(id, {}, failure());
    }
}
template <typename T>
void HostDebug::watch(quint64 id, const QFuture<T>& future,
                      std::shared_ptr<DebugAdapterDescriptorFactory> provider) {
    auto* watcher = new QFutureWatcher<T>(this);
    connect(
        watcher, &QFutureWatcher<T>::finished, this,
        [this, watcher, id, provider = std::move(provider)] {
            // Keep a native provider alive while its asynchronous method runs, even
            // after disposal/cancellation. A late inline result still needs dispose,
            // but must not produce another result or open an IPC endpoint.
            try {
                auto completed = watcher->future();
                completed.waitForFinished(); // finished signal; also rethrows provider exceptions
                if (completed.isCanceled())
                    complete(id, {}, "Debug adapter descriptor factory was canceled.");
                else if (!completed.resultCount())
                    complete(id, {}, "Debug adapter descriptor factory returned an empty future.");
                else
                    complete(id, completed.result(), {});
            } catch (...) {
                complete(id, {}, failure());
            }
            watcher->deleteLater();
        });
    watcher->setFuture(future);
}
void HostDebug::complete(quint64 id, const std::optional<DebugAdapterDescriptor>& descriptor,
                         const QString& error) {
    if (!pending_.remove(id)) {
        disposeUnused(descriptor);
        return;
    }
    QJsonObject message{
        {"event", "debugAdapterResult"}, {"requestId", QString::number(id)}, {"error", error}};
    if (error.isEmpty()) {
        try {
            if (!descriptor)
                throw std::runtime_error(
                    "Could not find a debug adapter descriptor for this debug type.");
            if (const auto* inlineAdapter =
                    std::get_if<DebugAdapterInlineImplementation>(&*descriptor)) {
                auto* bridge = new HostInlineDebugAdapter(inlineAdapter->implementation, this);
                inlineAdapters_.insert(id, bridge);
                connect(bridge, &QObject::destroyed, this,
                        [this, id] { inlineAdapters_.remove(id); });
                message.insert("descriptor", QJsonObject{{"type", "implementation"},
                                                         {"endpoint", bridge->endpoint()}});
            } else
                message.insert("descriptor", wire::encodeDebugAdapterDescriptor(*descriptor));
        } catch (...) {
            message.insert("error", failure());
        }
    }
    send_(message);
}
void HostDebug::release(quint64 id) {
    if (pending_.contains(id))
        complete(id, {}, "Debug adapter resolution was canceled.");
    const auto bridge = inlineAdapters_.take(id);
    if (bridge)
        bridge->stop();
}
void HostDebug::shutdown() {
    if (stopped_)
        return;
    stopped_ = true;
    configurations_->shutdown();
    sessions_->shutdown();
    trackers_->shutdown();
    starts_->shutdown();
    const auto pending = pending_.keys();
    for (const auto id : pending)
        complete(id, {}, "Extension host stopped before adapter descriptor completed.");
    factories_.clear();
    // Inline instances cannot outlive their owning extension's shutdown.
    const auto adapters = std::exchange(inlineAdapters_, {});
    for (const auto& bridge : adapters)
        if (bridge)
            bridge->stop();
}
} // namespace litecode::extensions
