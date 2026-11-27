#pragma once
#include "extensions/MessageApi.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <functional>

namespace litecode::extensions {
// Private workbench DTO; public extension items never cross this boundary.
struct WindowMessageRequest final {
    QString owner, source, text;
    quint64 id{};
    int severity{};
    MessageOptions options;
    QVector<MessageItem> items;
};
class WorkbenchMessages final : public QObject {
    Q_OBJECT
  public:
    using Reply = std::function<void(std::optional<int>, const QString&)>;
    using QObject::QObject;
    bool accept(const QString& owner, const QString& source, const QJsonObject& message,
                Reply reply);
    bool select(const QString& owner, quint64 id, std::optional<int> selection);
    void removeOwner(const QString& owner);
  signals:
    void requested(const litecode::extensions::WindowMessageRequest& request);
    void dismissed(const QString& owner, quint64 id);

  private:
    struct Pending {
        WindowMessageRequest request;
        Reply reply;
    };
    QHash<QString, QHash<quint64, Pending>> pending_;
};
} // namespace litecode::extensions
Q_DECLARE_METATYPE(litecode::extensions::WindowMessageRequest)
