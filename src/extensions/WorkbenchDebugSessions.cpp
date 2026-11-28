#include "extensions/WorkbenchDebugSessions.h"
#include <QFutureWatcher>
#include <QSet>
#include <limits>
#include <stdexcept>

namespace litecode::extensions {
bool WorkbenchDebugSessions::request(const QString& owner, const QJsonObject& message,
                                     Reply reply) {
    bool valid{};
    const auto text = message.value("requestId").toString();
    const auto id = text.toULongLong(&valid);
    const auto session = message.value("sessionId").toString();
    const auto operation = message.value("operation").toString();
    const auto command = message.value("dapCommand").toString();
    if (!valid || !id || QString::number(id) != text || session.isEmpty() ||
        !message.value("dapCommand").isString() ||
        (operation != "customRequest" && operation != "setName" && operation != "stop") ||
        (operation == "customRequest" && (command.isEmpty() || command.contains(QChar('\0')))) ||
        (operation == "setName" && (!message.value("arguments").isString() ||
                                    message.value("arguments").toString().isEmpty() ||
                                    message.value("arguments").toString().contains(QChar('\0')))))
        return false;
    for (const auto& pending : pending_)
        if (pending.owner == owner && pending.requestId == id)
            return false;
    const auto sendError = [&reply, text](const QString& error) {
        reply({{"command", "debugSessionResult"}, {"requestId", text}, {"error", error}});
    };
    if (!handler_ || !sessions_.contains(session)) {
        sendError("Debug session is not available.");
        return true;
    }
    if (pending_.size() >= 1024 || nextId_ == std::numeric_limits<quint64>::max()) {
        sendError("Too many pending debug requests.");
        return true;
    }
    const auto callId = ++nextId_;
    pending_.insert(callId, {owner, session, operation, id, std::move(reply)});
    try {
        const auto handler = handler_;
        const auto future = handler(session, operation, command, message.value("arguments"));
        if (!future.isValid()) {
            complete(callId, {}, "Debug backend returned no request result.");
            return true;
        }
        auto* watcher = new QFutureWatcher<QJsonValue>(this);
        connect(watcher, &QFutureWatcher<QJsonValue>::finished, this, [this, watcher, callId] {
            watcher->deleteLater();
            if (!pending_.contains(callId))
                return;
            try {
                if (watcher->future().isCanceled()) {
                    // Qt stores exceptions as cancellation. Read the result to
                    // preserve an adapter's actual rejection message.
                    (void)watcher->future().result();
                    complete(callId, {}, "Debug request was canceled.");
                } else if (watcher->future().resultCount() != 1)
                    complete(callId, {}, "Debug request was canceled.");
                else
                    complete(callId, watcher->result(), {});
            } catch (const std::exception& error) {
                complete(callId, {}, QString::fromUtf8(error.what()));
            } catch (...) {
                complete(callId, {}, "Debug request failed.");
            }
        });
        watcher->setFuture(future);
    } catch (const std::exception& error) {
        complete(callId, {}, QString::fromUtf8(error.what()));
    } catch (...) {
        complete(callId, {}, "Debug request failed.");
    }
    return true;
}
void WorkbenchDebugSessions::complete(quint64 callId, const QJsonValue& result,
                                      const QString& error) {
    const auto found = pending_.find(callId);
    if (found == pending_.end())
        return;
    const auto pending = found.value();
    pending_.erase(found);
    QJsonObject response{{"command", "debugSessionResult"},
                         {"requestId", QString::number(pending.requestId)},
                         {"error", error}};
    if (!result.isUndefined())
        response.insert("result", result);
    pending.reply(response);
}
void WorkbenchDebugSessions::removeOwner(const QString& owner) {
    const auto ids = pending_.keys();
    for (const auto id : ids)
        if (pending_.value(id).owner == owner)
            pending_.remove(id);
}
bool WorkbenchDebugSessions::publish(const QJsonObject& message) {
    const auto kind = message.value("kind").toString();
    if (kind == "active" && message.value("session").isNull()) {
        activeId_.clear();
        return true;
    }
    const auto session = message.value("session").toObject();
    const auto id = session.value("id").toString();
    if (id.isEmpty())
        return false;
    if (kind == "started" || kind == "changed")
        sessions_.insert(id, session);
    else if (kind == "active") {
        if (!sessions_.contains(id))
            return false;
        sessions_.insert(id, session);
        activeId_ = id;
    } else if (kind == "terminated" || kind == "discarded") {
        sessions_.remove(id);
        if (activeId_ == id)
            activeId_.clear();
        const auto calls = pending_.keys();
        for (const auto call : calls)
            if (pending_.value(call).sessionId == id) {
                const auto stopping = pending_.value(call).operation == "stop";
                complete(call, QJsonValue::Undefined,
                         stopping ? QString() : QStringLiteral("Debug session terminated."));
            }
    } else if (kind != "custom" || !sessions_.contains(id))
        return false;
    return true;
}
QVector<QJsonObject> WorkbenchDebugSessions::snapshot() const {
    QVector<QJsonObject> result;
    for (const auto& session : sessions_)
        result.append(
            {{"command", "debugSessionEvent"}, {"kind", "snapshot"}, {"session", session}});
    result.append({{"command", "debugSessionEvent"},
                   {"kind", "active"},
                   {"session", activeId_.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                   : QJsonValue(sessions_.value(activeId_))}});
    return result;
}
} // namespace litecode::extensions
