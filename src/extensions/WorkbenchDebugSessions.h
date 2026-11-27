#pragma once
#include <QObject>
#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <functional>

namespace litecode::extensions {
class WorkbenchDebugSessions final : public QObject {
  public:
    using Handler = std::function<QFuture<QJsonValue>(const QString&, const QString&, const QString&, const QJsonValue&)>;
    using Reply = std::function<void(const QJsonObject&)>;
    explicit WorkbenchDebugSessions(QObject* parent = nullptr) : QObject(parent) {}
    void setHandler(Handler handler) { handler_ = std::move(handler); }
    bool request(const QString& owner, const QJsonObject& message, Reply reply);
    void removeOwner(const QString& owner);
    bool publish(const QJsonObject& event);
    QVector<QJsonObject> snapshot() const;
  private:
    struct Pending { QString owner, sessionId, operation; quint64 requestId{}; Reply reply; };
    void complete(quint64 callId, const QJsonValue& result, const QString& error);
    QHash<QString, QJsonObject> sessions_;
    QHash<quint64, Pending> pending_;
    QString activeId_;
    Handler handler_;
    quint64 nextId_{};
};
} // namespace litecode::extensions
