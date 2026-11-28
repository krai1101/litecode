#pragma once
#include "extensions/DebugApi.h"
#include <QHash>
#include <QObject>
#include <QPromise>

namespace litecode::extensions {
// Private RPC owner. Facades retain their identity through factory resolution,
// lifecycle events and custom requests; none owns an adapter process.
class HostDebugSessions final : public QObject {
  public:
    explicit HostDebugSessions(std::function<void(const QJsonObject&)> send,
                               QObject* parent = nullptr);
    ~HostDebugSessions() override;
    std::shared_ptr<DebugSession> session(const QJsonObject& context);
    bool accept(const QJsonObject& message);
    void shutdown();
    std::shared_ptr<DebugSession> active() const { return active_; }
    Event<std::shared_ptr<DebugSession>> started() const { return started_.event(); }
    Event<std::shared_ptr<DebugSession>> terminated() const { return terminated_.event(); }
    Event<std::shared_ptr<DebugSession>> activeChanged() const { return activeChanged_.event(); }
    Event<DebugSessionCustomEvent> customEvent() const { return customEvent_.event(); }
    QFuture<QJsonValue> request(const QString& sessionId, const QString& operation,
                                const QString& command, const QJsonValue& args);

  private:
    class Session;
    struct Pending {
        QString sessionId;
        std::shared_ptr<QPromise<QJsonValue>> promise;
    };
    void failRequests(const QString& sessionId, const QString& error);
    std::function<void(const QJsonObject&)> send_;
    QHash<QString, std::shared_ptr<Session>> sessions_;
    QHash<quint64, Pending> pending_;
    std::shared_ptr<DebugSession> active_;
    EventEmitter<std::shared_ptr<DebugSession>> started_, terminated_, activeChanged_;
    EventEmitter<DebugSessionCustomEvent> customEvent_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
