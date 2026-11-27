#include "extensions/HostDebugSessions.h"
#include <QPointer>
#include <QThread>
#include <QSet>
#include <limits>
#include <stdexcept>

namespace litecode::extensions {
namespace {
void reject(const std::shared_ptr<QPromise<QJsonValue>>& promise, const QString& error) {
    promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    promise->finish();
}
bool validText(const QJsonValue& value) {
    return value.isString() && !value.toString().isEmpty() && !value.toString().contains(QChar('\0'));
}
}
class HostDebugSessions::Session final : public DebugSession {
  public:
    Session(HostDebugSessions* owner, const QJsonObject& value) : owner_(owner) { update(value); }
    void update(const QJsonObject& value) {
        for (const auto& key : {"id", "type", "name"})
            if (!validText(value.value(key))) throw std::invalid_argument("Invalid debug session context.");
        if (!state_.isEmpty() && (state_.value("id") != value.value("id") || state_.value("type") != value.value("type")))
            throw std::invalid_argument("Debug session identity cannot change.");
        if (!value.value("configuration").isObject() || value.value("configuration").toObject().value("type") != value.value("type"))
            throw std::invalid_argument("Invalid debug session configuration.");
        std::optional<WorkspaceFolder> folder;
        const auto field = value.value("workspaceFolder");
        if (!field.isNull()) {
            const auto data = field.toObject();
            const QUrl uri(data.value("uri").toString(), QUrl::StrictMode);
            const auto index = data.value("index");
            if (!field.isObject() || !validText(data.value("uri")) || !uri.isValid() || uri.scheme().isEmpty()
                || !data.value("name").isString() || !index.isDouble() || index.toDouble() != index.toInt(-1) || index.toInt(-1) < 0)
                throw std::invalid_argument("Invalid debug session workspace folder.");
            folder = WorkspaceFolder{uri, data.value("name").toString(), index.toInt()};
        }
        state_ = value; folder_ = folder;
    }
    QString id() const override { return state_.value("id").toString(); }
    QString type() const override { return state_.value("type").toString(); }
    QString name() const override { return state_.value("name").toString(); }
    void name(const QString& value) override {
        if (value.isEmpty() || value.contains(QChar('\0'))) throw std::invalid_argument("Invalid debug session name.");
        if (!owner_) throw std::runtime_error("Extension host is unavailable.");
        owner_->request(id(), "setName", {}, value);
        state_.insert("name", value);
    }
    std::shared_ptr<DebugSession> parentSession() const override { return parent_.lock(); }
    std::optional<WorkspaceFolder> workspaceFolder() const override { return folder_; }
    QJsonObject configuration() const override { return state_.value("configuration").toObject(); }
    QFuture<QJsonValue> customRequest(const QString& command, const QJsonValue& args) override {
        if (!owner_) {
            auto promise = std::make_shared<QPromise<QJsonValue>>(); promise->start();
            auto result = promise->future(); reject(promise, "Extension host is unavailable."); return result;
        }
        return owner_->request(id(), "customRequest", command, args);
    }
    std::weak_ptr<DebugSession> parent_;
    bool started{};
  private:
    QPointer<HostDebugSessions> owner_;
    QJsonObject state_;
    std::optional<WorkspaceFolder> folder_;
};
HostDebugSessions::HostDebugSessions(std::function<void(const QJsonObject&)> send, QObject* parent)
    : QObject(parent), send_(std::move(send)) {}
HostDebugSessions::~HostDebugSessions() { shutdown(); }
std::shared_ptr<DebugSession> HostDebugSessions::session(const QJsonObject& context) {
    if (QThread::currentThread() != thread()) throw std::logic_error("Debug sessions must use the host dispatcher.");
    const auto id = context.value("id").toString();
    auto item = sessions_.value(id);
    if (item) item->update(context);
    else { item = std::make_shared<Session>(this, context); sessions_.insert(id, item); }
    const auto parent = context.value("parentSession");
    if (!parent.isUndefined() && !parent.isNull()) {
        if (!parent.isString() || parent.toString() == id || !sessions_.contains(parent.toString()))
            throw std::invalid_argument("Invalid debug parent session.");
        item->parent_ = sessions_.value(parent.toString());
    }
    return item;
}
QFuture<QJsonValue> HostDebugSessions::request(const QString& sessionId, const QString& operation,
    const QString& command, const QJsonValue& args) {
    if (QThread::currentThread() != thread()) throw std::logic_error("Debug requests must use the host dispatcher.");
    auto promise = std::make_shared<QPromise<QJsonValue>>(); promise->start();
    auto future = promise->future();
    if (stopped_ || !sessions_.contains(sessionId)) { reject(promise, "Debug session is not available."); return future; }
    if (operation == "customRequest" && (command.isEmpty() || command.contains(QChar('\0')))) {
        reject(promise, "Invalid debug request command."); return future;
    }
    if (pending_.size() >= 1024 || nextId_ == std::numeric_limits<quint64>::max()) {
        reject(promise, "Debug request capacity exhausted."); return future;
    }
    const auto id = ++nextId_;
    pending_.insert(id, {sessionId, promise});
    QJsonObject message{{"event", "debugSessionRequest"}, {"requestId", QString::number(id)},
        {"sessionId", sessionId}, {"operation", operation}, {"dapCommand", command}};
    if (!args.isUndefined()) message.insert("arguments", args);
    send_(message);
    return future;
}
void HostDebugSessions::failRequests(const QString& sessionId, const QString& error) {
    const auto ids = pending_.keys();
    for (const auto id : ids) {
        const auto it = pending_.find(id);
        if (it == pending_.end() || (!sessionId.isEmpty() && it->sessionId != sessionId)) continue;
        const auto promise = it->promise; pending_.erase(it); reject(promise, error);
    }
}
bool HostDebugSessions::accept(const QJsonObject& message) {
    if (stopped_) return true;
    const auto command = message.value("command").toString();
    if (command == "debugSessionResult") {
        bool valid{}; const auto text = message.value("requestId").toString(); const auto id = text.toULongLong(&valid);
        if (!valid || !id || QString::number(id) != text || !message.value("error").isString()) return false;
        const auto found = pending_.find(id);
        if (found == pending_.end()) return true; // Session ended before a late response.
        const auto promise = found->promise; pending_.erase(found);
        const auto error = message.value("error").toString();
        if (!error.isEmpty()) reject(promise, error);
        else { promise->addResult(message.value("result")); promise->finish(); }
        return true;
    }
    if (command != "debugSessionEvent") return false;
    const auto kind = message.value("kind").toString();
    if (kind == "active" && message.value("session").isNull()) {
        if (active_) { active_.reset(); activeChanged_.fire(active_); } return true;
    }
    if (!message.value("session").isObject()) return false;
    const auto id = message.value("session").toObject().value("id").toString();
    if ((kind == "terminated" || kind == "discarded") && !sessions_.contains(id)) return true;
    if ((kind == "custom" || kind == "changed") && !sessions_.contains(id)) return false;
    try {
        auto item = session(message.value("session").toObject());
        if (kind == "snapshot") {
            sessions_.value(item->id())->started = true;
        } else if (kind == "started") {
            auto native = sessions_.value(item->id());
            if (!native->started) { native->started = true; started_.fire(item); }
        } else if (kind == "active") {
            if (active_ != item) { active_ = item; activeChanged_.fire(item); }
        } else if (kind == "terminated" || kind == "discarded") {
            failRequests(item->id(), "Debug session terminated.");
            if (active_ == item) { active_.reset(); activeChanged_.fire(active_); }
            sessions_.remove(item->id());
            if (kind == "terminated") terminated_.fire(item);
        } else if (kind == "custom") {
            if (!validText(message.value("event"))) return false;
            customEvent_.fire({item, message.value("event").toString(), message.value("body")});
        } else if (kind != "changed") return false;
        return true;
    } catch (...) { return false; }
}
void HostDebugSessions::shutdown() {
    if (stopped_) return;
    stopped_ = true; failRequests({}, "Extension host stopped.");
    active_.reset(); sessions_.clear();
    started_.dispose(); terminated_.dispose(); activeChanged_.dispose(); customEvent_.dispose();
}
} // namespace litecode::extensions
