#pragma once
#include "extensions/StatusBarWire.h"
#include <QHash>
#include <QObject>
#include <QVector>
namespace litecode::extensions {
struct StatusBarEntryInfo final {
    QString key, owner, id, name;
    quint64 token{}, order{};
    wire::StatusBarState state;
};
class WorkbenchStatusBar final : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    bool accept(const QString& owner, const QString& displayName, const QJsonObject& message);
    void removeOwner(const QString& owner);
    [[nodiscard]] QVector<StatusBarEntryInfo> entries() const;
    [[nodiscard]] std::optional<StatusBarEntryInfo> entry(const QString& key) const;
    bool invoke(const QString& key);
  signals:
    void changed();
    void commandRequested(const QString& command, const QJsonArray& arguments);
    void error(const QString& text);

  private:
    struct Message {
        QString owner, text;
        quint64 token{}, order{};
    };
    QHash<QString, StatusBarEntryInfo> entries_;
    QHash<QString, Message> messages_;
    quint64 nextOrder_{};
};
} // namespace litecode::extensions
