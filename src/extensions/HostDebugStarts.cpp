#include "extensions/HostDebugStarts.h"
#include <QThread>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
namespace {
void reject(const std::shared_ptr<QPromise<bool>>& promise, const QString& error) {
    promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    promise->finish();
}
}
HostDebugStarts::HostDebugStarts(std::function<void(const QJsonObject&)> send, QObject* parent)
    : QObject(parent), send_(std::move(send)) {}
HostDebugStarts::~HostDebugStarts() { shutdown(); }
QFuture<bool> HostDebugStarts::start(std::optional<WorkspaceFolder> folder,
    const std::variant<QString, QJsonObject>& configuration,
    const std::variant<std::shared_ptr<DebugSession>, DebugSessionOptions>& parentOrOptions) {
    if (QThread::currentThread() != thread()) throw std::logic_error("Debug starts must use the host dispatcher.");
    auto promise = std::make_shared<QPromise<bool>>(); promise->start(); auto future = promise->future();
    if (stopped_) { reject(promise, "Extension host stopped."); return future; }
    if (folder && (!folder->uri.isValid() || !folder->uri.isLocalFile() || folder->index < 0)) {
        reject(promise, "Invalid debug workspace folder."); return future;
    }
    if (const auto* name = std::get_if<QString>(&configuration); name && (name->isEmpty() || name->contains(QChar('\0')))) {
        reject(promise, "Invalid launch configuration name."); return future;
    }
    if (pending_.size() >= 1024 || nextId_ == std::numeric_limits<quint64>::max()) {
        reject(promise, "Debug start capacity exhausted."); return future;
    }
    DebugSessionOptions options;
    if (const auto* value = std::get_if<DebugSessionOptions>(&parentOrOptions)) options = *value;
    else options.parentSession = std::get<std::shared_ptr<DebugSession>>(parentOrOptions);
    if (options.testRun) { reject(promise, "Test-run debug integration is not available."); return future; }
    QJsonObject encoded;
    if (options.parentSession) encoded.insert("parentSession", options.parentSession->id());
    const auto add = [&encoded](const char* key, const std::optional<bool>& value) {
        if (value) encoded.insert(key, *value);
    };
    add("lifecycleManagedByParent", options.lifecycleManagedByParent);
    add("noDebug", options.noDebug); add("compact", options.compact);
    add("suppressSaveBeforeStart", options.suppressSaveBeforeStart);
    add("suppressDebugToolbar", options.suppressDebugToolbar);
    add("suppressDebugStatusbar", options.suppressDebugStatusbar);
    add("suppressDebugView", options.suppressDebugView);
    if (options.consoleMode) {
        const auto value = static_cast<int>(*options.consoleMode);
        if (value != 0 && value != 1) { reject(promise, "Invalid debug console mode."); return future; }
        encoded.insert("consoleMode", value);
    }
    const auto id = ++nextId_;
    QJsonObject message{{"event", "debugStartRequest"}, {"requestId", QString::number(id)}, {"options", encoded}};
    if (folder) message.insert("folder", QJsonObject{{"uri", folder->uri.toString(QUrl::FullyEncoded)}, {"name", folder->name}, {"index", folder->index}});
    if (const auto* name = std::get_if<QString>(&configuration)) message.insert("configuration", *name);
    else message.insert("configuration", std::get<QJsonObject>(configuration));
    pending_.insert(id, promise); send_(message); return future;
}
bool HostDebugStarts::accept(const QJsonObject& message) {
    if (message.value("command") != QJsonValue("debugStartResult")) return false;
    bool valid{}; const auto text = message.value("requestId").toString(); const auto id = text.toULongLong(&valid);
    if (!valid || !id || QString::number(id) != text || !message.value("error").isString()
        || (message.value("error").toString().isEmpty() && !message.value("result").isBool())) return false;
    const auto promise = pending_.take(id);
    if (!promise) return true;
    if (!message.value("error").toString().isEmpty()) reject(promise, message.value("error").toString());
    else { promise->addResult(message.value("result").toBool()); promise->finish(); }
    return true;
}
void HostDebugStarts::shutdown() {
    if (std::exchange(stopped_, true)) return;
    const auto pending = std::exchange(pending_, {});
    for (const auto& promise : pending) reject(promise, "Extension host stopped.");
}
} // namespace litecode::extensions
