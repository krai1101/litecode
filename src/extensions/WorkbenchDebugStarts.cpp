#include "extensions/WorkbenchDebugStarts.h"
#include <QFutureWatcher>
#include <QUrl>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
WorkbenchDebugStarts::~WorkbenchDebugStarts() {
    const auto pending = std::exchange(pending_, {});
    for (const auto& item : pending) item.cancellation->cancel();
}
bool WorkbenchDebugStarts::request(const QString& owner, const QJsonObject& message, Reply reply) {
    bool valid{}; const auto text = message.value("requestId").toString(); const auto id = text.toULongLong(&valid);
    const auto configuration = message.value("configuration"); const auto folder = message.value("folder");
    if (!valid || !id || QString::number(id) != text || !message.value("options").isObject()
        || (!configuration.isString() && !configuration.isObject())) return false;
    if (!folder.isUndefined()) {
        const auto value = folder.toObject(); const QUrl uri(value.value("uri").toString(), QUrl::StrictMode);
        const auto index = value.value("index");
        if (!folder.isObject() || !value.value("uri").isString() || !uri.isValid() || !uri.isLocalFile()
            || !value.value("name").isString() || !index.isDouble() || index.toDouble() != index.toInt(-1) || index.toInt(-1) < 0) return false;
    }
    const auto options = message.value("options").toObject();
    for (auto it = options.begin(); it != options.end(); ++it) {
        if (it.key() == "parentSession") { if (!it.value().isString() || it.value().toString().isEmpty()) return false; }
        else if (it.key() == "consoleMode") { if (it.value() != QJsonValue(0) && it.value() != QJsonValue(1)) return false; }
        else if (it.key() == "noDebug" || it.key() == "compact" || it.key() == "lifecycleManagedByParent"
            || it.key() == "suppressSaveBeforeStart" || it.key() == "suppressDebugToolbar"
            || it.key() == "suppressDebugStatusbar" || it.key() == "suppressDebugView") { if (!it.value().isBool()) return false; }
        else return false;
    }
    for (const auto& item : pending_) if (item.owner == owner && item.requestId == text) return false;
    const auto sendError = [&reply, text](const QString& error) {
        reply({{"command", "debugStartResult"}, {"requestId", text}, {"error", error}});
    };
    if (!handler_) { sendError("Debug workbench is unavailable."); return true; }
    if (pending_.size() >= 1024 || nextId_ == std::numeric_limits<quint64>::max()) { sendError("Debug start capacity exhausted."); return true; }
    const auto call = ++nextId_;
    auto cancellation = std::make_shared<CancellationTokenSource>();
    pending_.insert(call, {owner, text, std::move(reply), cancellation, {}});
    try {
        const auto handler = handler_; const auto future = handler(folder, configuration, options, cancellation->token());
        if (!pending_.contains(call)) return true; // Handler may synchronously unload its caller.
        if (!future.isValid()) { complete(call, false, "Debug workbench returned no start result."); return true; }
        auto* watcher = new QFutureWatcher<bool>(this);
        pending_[call].watcher = watcher;
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, call] {
            watcher->deleteLater(); if (!pending_.contains(call)) return;
            try {
                if (watcher->future().resultCount() == 1) complete(call, watcher->result(), {});
                else {
                    // Reading an exception-backed future preserves its error.
                    if (watcher->future().isCanceled()) (void)watcher->future().result();
                    complete(call, false, {});
                }
            } catch (const std::exception& error) { complete(call, false, QString::fromUtf8(error.what())); }
            catch (...) { complete(call, false, "Debug launch failed."); }
        });
        watcher->setFuture(future);
    } catch (const std::exception& error) { complete(call, false, QString::fromUtf8(error.what())); }
    catch (...) { complete(call, false, "Debug launch failed."); }
    return true;
}
void WorkbenchDebugStarts::complete(quint64 id, bool result, const QString& error) {
    const auto found = pending_.find(id); if (found == pending_.end()) return;
    const auto item = found.value(); pending_.erase(found);
    item.reply({{"command", "debugStartResult"}, {"requestId", item.requestId}, {"result", result}, {"error", error}});
}
void WorkbenchDebugStarts::removeOwner(const QString& owner) {
    const auto keys = pending_.keys();
    for (const auto id : keys) {
        const auto found = pending_.find(id); if (found == pending_.end() || found->owner != owner) continue;
        const auto item = found.value(); pending_.erase(found);
        if (item.watcher) item.watcher->deleteLater();
        item.cancellation->cancel();
    }
}
} // namespace litecode::extensions
