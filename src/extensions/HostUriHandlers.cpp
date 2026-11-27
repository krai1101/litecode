#include "extensions/HostUriHandlers.h"
#include "extensions/UriWire.h"
#include "extensions/ExtensionWireProtocol.h"
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <limits>
#include <stdexcept>
#include <utility>
namespace litecode::extensions {
struct UriHandlerRuntime {
    QMutex mutex;
    QString owner;
    QObject* dispatcher{};
    std::function<void(const QJsonObject&)> send;
    std::shared_ptr<UriHandler> handler;
    quint64 token{};
    bool stopped{};
};
namespace {
void post(const std::shared_ptr<UriHandlerRuntime>& runtime, QJsonObject message) {
    if (runtime->stopped || !runtime->dispatcher) return;
    QMetaObject::invokeMethod(runtime->dispatcher, [runtime, message = std::move(message)] {
        std::function<void(const QJsonObject&)> send;
        { QMutexLocker lock(&runtime->mutex); if (runtime->stopped) return; send = runtime->send; }
        try { if (!wire::encode(message).isEmpty()) send(message); } catch (...) {}
    }, Qt::QueuedConnection);
}
}
HostUriHandlers::HostUriHandlers(QString owner, std::function<void(const QJsonObject&)> send)
    : runtime_(std::make_shared<UriHandlerRuntime>()) {
    runtime_->owner = std::move(owner); runtime_->send = std::move(send); runtime_->dispatcher = this;
}
HostUriHandlers::~HostUriHandlers() { shutdown(); }
Disposable HostUriHandlers::registerUriHandler(std::shared_ptr<UriHandler> handler) {
    if (!handler) throw std::invalid_argument("URI handler must be provided.");
    auto runtime = runtime_; QMutexLocker lock(&runtime->mutex);
    if (runtime->stopped) throw std::runtime_error("URI handler service has stopped.");
    if (runtime->handler) throw std::runtime_error("Protocol handler already registered for this extension.");
    if (runtime->token == std::numeric_limits<quint64>::max()) throw std::overflow_error("URI handler identifiers exhausted.");
    const auto token = ++runtime->token; runtime->handler = std::move(handler);
    post(runtime, {{"event", "uriHandler"}, {"op", "register"}, {"handle", QString::number(token)}});
    const std::weak_ptr<UriHandlerRuntime> weak(runtime);
    return Disposable([weak, token] {
        if (const auto alive = weak.lock()) {
            std::shared_ptr<UriHandler> handler;
            { QMutexLocker guard(&alive->mutex);
              if (!alive->handler || alive->token != token) return;
              handler = std::exchange(alive->handler, {});
              post(alive, {{"event", "uriHandler"}, {"op", "unregister"}, {"handle", QString::number(token)}}); }
        }
    });
}
bool HostUriHandlers::accept(const QJsonObject& message) {
    const auto text = message.value("handle").toString(); bool valid = false; const auto token = text.toULongLong(&valid);
    const auto uri = wire::decodeUri(message.value("uri"));
    if (!valid || !token || QString::number(token) != text || !uri || !wire::isExtensionUri(*uri, runtime_->owner)) return false;
    std::shared_ptr<UriHandler> handler;
    { QMutexLocker lock(&runtime_->mutex); if (runtime_->stopped || token != runtime_->token) return true; handler = runtime_->handler; }
    if (!handler) return true;
    const QPointer<HostUriHandlers> self(this);
    try {
        const auto result = handler->handleUri(*uri);
        if (!self) return true;
        if (const auto* future = std::get_if<QFuture<void>>(&result)) {
            auto* watcher = new QFutureWatcher<void>(this);
            connect(watcher, &QFutureWatcher<void>::finished, this, [self, watcher] {
                watcher->deleteLater();
                try { watcher->future().waitForFinished(); }
                catch (const std::exception& error) { if (self) self->reportError(QString::fromUtf8(error.what())); }
                catch (...) { if (self) self->reportError("URI handler failed."); }
            });
            watcher->setFuture(*future);
        }
    } catch (const std::exception& error) { if (self) self->reportError(QString::fromUtf8(error.what())); }
    catch (...) { if (self) self->reportError("URI handler failed."); }
    return true;
}
void HostUriHandlers::reportError(const QString& error) {
    QMutexLocker lock(&runtime_->mutex);
    post(runtime_, {{"event", "uriHandler"}, {"op", "error"}, {"text", error.left(65536)}});
}
void HostUriHandlers::shutdown() {
    std::shared_ptr<UriHandler> handler;
    { QMutexLocker lock(&runtime_->mutex); if (runtime_->stopped) return; runtime_->stopped = true;
      runtime_->dispatcher = nullptr; runtime_->send = {}; handler = std::exchange(runtime_->handler, {}); }
}
}
