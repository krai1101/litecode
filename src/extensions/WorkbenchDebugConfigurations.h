#pragma once
#include "extensions/DebugConfigurationApi.h"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <functional>

namespace litecode::extensions {
class WorkbenchDebugConfigurations final : public QObject {
    Q_OBJECT
  public:
    using Send = std::function<void(const QString&, const QJsonObject&)>;
    explicit WorkbenchDebugConfigurations(Send send, QObject* parent = nullptr);
    ~WorkbenchDebugConfigurations() override;
    bool registerProvider(const QString& owner, const QJsonObject& message);
    bool unregisterProvider(const QString& owner, quint64 handle);
    void ready(const QString& owner);
    void removeOwner(const QString& owner, const QString& error);
    quint64 request(const QString& type, const QString& method, const QJsonValue& configuration,
        const std::optional<WorkspaceFolder>& folder, const QStringList& awaitingOwners,
        DebugConfigurationProviderTriggerKind trigger = DebugConfigurationProviderTriggerKind::Initial);
    bool accept(const QString& owner, const QJsonObject& message);
    bool cancel(quint64 id);
  signals:
    void finished(quint64 id, const QJsonValue& result, const QString& error);
  private:
    struct Provider { QString owner; quint64 handle{}; QString type; DebugConfigurationProviderTriggerKind trigger{}; };
    struct Operation {
        QString type, method;
        QJsonValue configuration;
        std::optional<WorkspaceFolder> folder;
        QStringList awaitingOwners;
        DebugConfigurationProviderTriggerKind trigger{};
        QVector<Provider> chain;
        qsizetype next{};
        QJsonArray configurations;
        quint64 call{};
        bool started{};
    };
    struct Call { quint64 operation{}; Provider provider; };
    void advance(quint64 id);
    void complete(quint64 id, const QJsonValue& result, const QString& error = {});
    Send send_;
    QHash<QString, QHash<quint64, Provider>> providers_;
    QSet<QString> readyOwners_;
    QHash<quint64, Operation> operations_;
    QHash<quint64, Call> calls_;
    quint64 nextId_{}, nextCall_{};
};
} // namespace litecode::extensions
