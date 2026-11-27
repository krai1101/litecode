#include "extensions/WorkbenchUriHandlers.h"
#include "extensions/UriWire.h"
#include "extensions/ExtensionWireProtocol.h"
#include <QPointer>
#include <algorithm>
namespace litecode::extensions {
WorkbenchUriHandlers::WorkbenchUriHandlers(QObject* parent) : QObject(parent) {
    clock_.start(); collectTimer_.setInterval(30000);
    connect(&collectTimer_, &QTimer::timeout, this, &WorkbenchUriHandlers::collect); collectTimer_.start();
}
bool WorkbenchUriHandlers::accept(const QString& owner, const QJsonObject& message, Send sender) {
    if (owner.isEmpty() || !sender) return false;
    const auto op = message.value("op").toString();
    if (op == "error") {
        if (!message.value("text").isString() || message.value("text").toString().size() > 65536) return false;
        emit error(owner, message.value("text").toString()); return true;
    }
    const auto text = message.value("handle").toString(); bool valid = false; const auto token = text.toULongLong(&valid);
    if (!valid || !token || QString::number(token) != text) return false;
    const auto key = owner.toCaseFolded();
    if (op == "unregister") {
        auto found = handlers_.find(key);
        if (found != handlers_.end() && found->token == token) handlers_.erase(found);
        return true;
    }
    if (op != "register" || handlers_.contains(key)) return false;
    Handler handler{owner, token, std::move(sender)}; handlers_.insert(key, handler);
    const auto buffered = pending_.take(key); const QPointer<WorkbenchUriHandlers> self(this);
    for (const auto& entry : buffered) {
        if (!self) break;
        // Handler disposal/replacement during a delivery must stop this batch.
        const auto current = self->handlers_.constFind(key);
        if (current == self->handlers_.cend() || current->token != token) break;
        if (clock_.elapsed() - entry.timestamp < 300000) self->send(handler, entry.uri);
    }
    return true;
}
bool WorkbenchUriHandlers::send(const Handler& handler, const QUrl& uri) {
    const QJsonObject message{{"command", "handleExternalUri"}, {"handle", QString::number(handler.token)}, {"uri", wire::encodeUri(uri)}};
    if (wire::encode(message).isEmpty()) { emit error(handler.owner, "Incoming URI exceeds the transport limit."); return false; }
    const QPointer<WorkbenchUriHandlers> self(this);
    try { handler.send(message); if (self) emit self->dispatched(handler.owner); return true; }
    catch (...) { if (self) emit self->error(handler.owner, "Could not deliver incoming URI."); return false; }
}
bool WorkbenchUriHandlers::dispatch(const QString& owner, const QUrl& uri) {
    if (!wire::isExtensionUri(uri, owner)) return false;
    const auto key = owner.toCaseFolded();
    const auto found = handlers_.constFind(key);
    if (found != handlers_.cend()) { const auto handler = *found; return send(handler, uri); }
    collect();
    pending_[key].append({uri, clock_.elapsed()}); emit activationRequested(owner); return true;
}
void WorkbenchUriHandlers::removeOwner(const QString& owner) {
    const auto key = owner.toCaseFolded(); handlers_.remove(key); pending_.remove(key);
}
void WorkbenchUriHandlers::collect() {
    const auto now = clock_.elapsed();
    for (auto buffered = pending_.begin(); buffered != pending_.end();) {
        auto& entries = *buffered;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [now](const Buffered& entry) { return now - entry.timestamp >= 300000; }), entries.end());
        if (entries.isEmpty()) buffered = pending_.erase(buffered); else ++buffered;
    }
}
}
