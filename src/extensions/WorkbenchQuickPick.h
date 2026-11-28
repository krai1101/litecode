#pragma once
#include "extensions/QuickPickApi.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
namespace litecode::extensions {
struct QuickPickRequest final {
    QString owner;
    quint64 id{};
    QuickPickOptions options;
};
class WorkbenchQuickPick final : public QObject {
    Q_OBJECT
  public:
    using Send = std::function<void(const QJsonObject&)>;
    using QObject::QObject;
    bool accept(const QString& owner, const QJsonObject& message, Send send);
    bool complete(const QString& owner, quint64 id, std::optional<QVector<int>> selection,
                  const QString& error = {});
    bool focus(const QString& owner, quint64 id, int index);
    void removeOwner(const QString& owner);
    [[nodiscard]] bool isPending(const QString& owner, quint64 id) const {
        const auto requests = pending_.constFind(owner);
        return requests != pending_.cend() && requests->contains(id);
    }
  signals:
    void requested(const litecode::extensions::QuickPickRequest& request);
    void itemsChanged(const QString& owner, quint64 id,
                      const QVector<litecode::extensions::QuickPickItem>& items);
    void dismissed(const QString& owner, quint64 id);

  private:
    struct Pending {
        QuickPickRequest request;
        Send send;
        QVector<QuickPickItem> items;
        bool loaded{};
    };
    QHash<QString, QHash<quint64, Pending>> pending_;
};
} // namespace litecode::extensions
Q_DECLARE_METATYPE(litecode::extensions::QuickPickRequest)
Q_DECLARE_METATYPE(QVector<litecode::extensions::QuickPickItem>)
