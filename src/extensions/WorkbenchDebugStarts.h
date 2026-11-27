#pragma once
#include "extensions/CancellationApi.h"
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>

namespace litecode::extensions {
class WorkbenchDebugStarts final : public QObject {
  public:
    using Handler = std::function<QFuture<bool>(const QJsonValue&, const QJsonValue&,
                                                const QJsonObject&, CancellationToken)>;
    using Reply = std::function<void(const QJsonObject&)>;
    explicit WorkbenchDebugStarts(QObject* parent = nullptr) : QObject(parent) {}
    ~WorkbenchDebugStarts() override;
    void setHandler(Handler handler) { handler_ = std::move(handler); }
    bool request(const QString& owner, const QJsonObject& message, Reply reply);
    void removeOwner(const QString& owner);

  private:
    struct Pending {
        QString owner, requestId;
        Reply reply;
        std::shared_ptr<CancellationTokenSource> cancellation;
        QPointer<QFutureWatcher<bool>> watcher;
    };
    void complete(quint64 id, bool result, const QString& error);
    QHash<quint64, Pending> pending_;
    Handler handler_;
    quint64 nextId_{};
};
} // namespace litecode::extensions
