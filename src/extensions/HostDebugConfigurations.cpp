#include "extensions/HostDebugConfigurations.h"
#include <QFutureWatcher>
#include <QJsonArray>
#include <QPointer>
#include <QThread>
#include <limits>
#include <stdexcept>

namespace litecode::extensions {
namespace {
QString failure() {
    try { throw; } catch (const std::exception& error) { return QString::fromUtf8(error.what()); }
    catch (...) { return "Debug configuration provider failed."; }
}
QJsonValue resultValue(const DebugConfiguration& value) {
    if (!value.isObject() && !value.isNull() && !value.isUndefined())
        throw std::invalid_argument("Debug configuration provider returned a non-object configuration.");
    return value;
}
QJsonValue resultValue(const QVector<DebugConfiguration>& values) {
    QJsonArray result;
    for (const auto& value : values) {
        if (!value.isObject()) throw std::invalid_argument("Debug configuration provider returned a non-object array entry.");
        result.append(value);
    }
    return result;
}
template<typename T> QJsonValue resultValue(const std::optional<T>& value) {
    return value ? resultValue(*value) : QJsonValue(QJsonValue::Undefined);
}
}
HostDebugConfigurations::HostDebugConfigurations(std::function<void(const QJsonObject&)> send, QObject* parent)
    : QObject(parent), send_(std::move(send)) {}
HostDebugConfigurations::~HostDebugConfigurations() { shutdown(); }
Disposable HostDebugConfigurations::registerDebugConfigurationProvider(const QString& type,
    std::shared_ptr<DebugConfigurationProvider> provider, DebugConfigurationProviderTriggerKind trigger) {
    if (QThread::currentThread() != thread()) throw std::logic_error("Debug registration must run on the host dispatcher.");
    if (!provider) return {};
    if (stopped_ || type.contains(QChar('\0')) || (trigger != DebugConfigurationProviderTriggerKind::Initial
        && trigger != DebugConfigurationProviderTriggerKind::Dynamic)) throw std::invalid_argument("Invalid debug configuration registration.");
    if (nextHandle_ == std::numeric_limits<quint64>::max()) throw std::overflow_error("Debug configuration provider handles exhausted.");
    const auto handle = ++nextHandle_;
    providers_.insert(handle, {type, std::move(provider)});
    send_({{"event", "registeredDebugConfigurationProvider"}, {"handle", QString::number(handle)},
        {"type", type}, {"triggerKind", static_cast<int>(trigger)}});
    const QPointer<HostDebugConfigurations> self(this);
    return Disposable([self, handle] {
        if (!self) return;
        if (QThread::currentThread() == self->thread()) self->remove(handle);
        else QMetaObject::invokeMethod(self, [self, handle] { if (self) self->remove(handle); }, Qt::QueuedConnection);
    });
}
void HostDebugConfigurations::remove(quint64 handle) {
    if (!providers_.remove(handle)) return;
    // Detach pending requests before entering cancellation listeners, which can
    // reenter registration/disposal. Each request settles at most once.
    const auto pending = pending_;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        if (it->handle == handle) cancel(it.key());
    if (!stopped_) send_({{"event", "unregisteredDebugConfigurationProvider"}, {"handle", QString::number(handle)}});
}
void HostDebugConfigurations::invoke(quint64 id, quint64 handle, const QString& method,
    const std::optional<WorkspaceFolder>& folder, const QJsonValue& configuration) {
    if (stopped_ || !id || pending_.contains(id)) return;
    auto cancellation = std::make_shared<CancellationTokenSource>();
    pending_.insert(id, {handle, cancellation});
    try {
        const auto provider = providers_.value(handle).provider;
        if (!provider) throw std::invalid_argument("Debug configuration provider is no longer registered.");
        if (method == "provideDebugConfigurations")
            settle<QVector<DebugConfiguration>>(id, provider->provideDebugConfigurations(folder, cancellation->token()), provider);
        else if (method == "resolveDebugConfiguration" || method == "resolveDebugConfigurationWithSubstitutedVariables") {
            if (!configuration.isObject()) throw std::invalid_argument("A configuration resolution request must contain an object.");
            settle<DebugConfiguration>(id, method == "resolveDebugConfiguration"
                ? provider->resolveDebugConfiguration(folder, configuration, cancellation->token())
                : provider->resolveDebugConfigurationWithSubstitutedVariables(folder, configuration, cancellation->token()), provider);
        } else throw std::invalid_argument("Unknown debug configuration provider method.");
    } catch (...) { complete(id, {}, failure()); }
}
template<typename T> void HostDebugConfigurations::settle(quint64 id, ProviderResult<T> result,
    std::shared_ptr<DebugConfigurationProvider> provider) {
    std::visit([this, id, provider = std::move(provider)](auto&& value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, T> || std::is_same_v<Value, std::optional<T>>) complete(id, resultValue(value));
        else {
            using Result = std::conditional_t<std::is_same_v<Value, QFuture<T>>, T, std::optional<T>>;
            auto* watcher = new QFutureWatcher<Result>(this);
            connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, id, provider] {
                if (pending_.contains(id)) {
                    try {
                        auto future = watcher->future(); future.waitForFinished();
                        if (future.isCanceled()) complete(id, QJsonValue(QJsonValue::Undefined));
                        else if (!future.resultCount()) complete(id, {}, "Debug configuration provider returned an empty future.");
                        else complete(id, resultValue(future.result()));
                    } catch (...) { complete(id, {}, failure()); }
                }
                watcher->deleteLater();
            });
            watcher->setFuture(value);
        }
    }, std::move(result));
}
void HostDebugConfigurations::complete(quint64 id, const QJsonValue& result, const QString& error) {
    if (!pending_.remove(id)) return;
    QJsonObject message{{"event", "debugConfigurationResult"}, {"requestId", QString::number(id)}, {"error", error}};
    // JSON omits undefined. Explicit state prevents null cancellation from
    // becoming indistinguishable from an absent result across the host boundary.
    message.insert("resultKind", result.isUndefined() ? "undefined" : result.isNull() ? "null" : "value");
    if (!result.isUndefined()) message.insert("result", result);
    send_(message);
}
void HostDebugConfigurations::cancel(quint64 id) {
    const auto found = pending_.find(id);
    if (found == pending_.end()) return;
    auto cancellation = found->cancellation;
    complete(id, QJsonValue(QJsonValue::Undefined));
    cancellation->cancel();
}
void HostDebugConfigurations::shutdown() {
    if (stopped_) return;
    stopped_ = true;
    const auto pending = pending_.keys();
    for (const auto id : pending) cancel(id);
    providers_.clear();
}
} // namespace litecode::extensions
