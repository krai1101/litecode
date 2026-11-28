#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <functional>

namespace litecode::extensions {
class WorkbenchDebugTrackers final : public QObject {
    Q_OBJECT
  public:
    explicit WorkbenchDebugTrackers(std::function<void(const QString&, const QJsonObject&)> send,
                                    QObject* parent = nullptr);
    bool registerFactory(const QString& owner, const QJsonObject& message);
    bool unregisterFactory(const QString& owner, quint64 handle);
    void removeOwner(const QString& owner);
    void prepare(quint64 requestId, const QString& adapterOwner, const QJsonObject& session,
                 const QJsonObject& descriptor);
    bool acceptReady(const QString& owner, quint64 requestId);
    bool release(quint64 requestId);
    void publish(const QString& sessionId, const QJsonObject& event);
  signals:
    void prepared(quint64 requestId, const QJsonObject& descriptor, const QString& error);
    void adapterOwnerUnavailable(const QString& sessionId);

  private:
    struct Group {
        QJsonObject session, descriptor;
        QString adapterOwner;
        QSet<QString> owners, waiting;
        bool ready{}, started{};
    };
    void finish(quint64 id, bool timeout = false);
    void cancelOwners(quint64 id, const QSet<QString>& owners);
    std::function<void(const QString&, const QJsonObject&)> send_;
    QHash<QString, QHash<quint64, QString>> factories_;
    QHash<quint64, Group> groups_;
};
} // namespace litecode::extensions
