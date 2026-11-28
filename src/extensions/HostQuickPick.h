#pragma once
#include "extensions/QuickPickApi.h"
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPromise>
namespace litecode::extensions {
class HostQuickPick final : public QObject {
  public:
    explicit HostQuickPick(std::function<void(const QJsonObject&)> send);
    ~HostQuickPick() override;
    detail::QuickPickSession begin(const QuickPickOptions& options, CancellationToken token,
                                   std::function<void(int)> focus);
    bool accept(const QJsonObject& message);
    void shutdown();

  private:
    struct Pending {
        std::shared_ptr<QPromise<std::optional<QVector<int>>>> promise;
        std::function<void(int)> focus;
        Disposable cancellation;
        QVector<QuickPickItem> items;
        bool many{}, loaded{}, shown{};
    };
    void setItems(quint64 id, QVector<QuickPickItem> items);
    void finish(quint64 id, std::optional<QVector<int>> selection, std::exception_ptr error = {},
                bool dismiss = false);
    void transmit(quint64 id, const QJsonObject& message);
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QHash<quint64, Pending> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
