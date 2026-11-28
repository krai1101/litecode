#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>
namespace litecode::extensions {
struct ExternalUriRequest final {
    QString owner;
    quint64 id{};
    QUrl uri;
    bool open{};
};
class WorkbenchExternalUris final : public QObject {
    Q_OBJECT
  public:
    using Send = std::function<void(const QJsonObject&)>;
    using QObject::QObject;
    bool accept(const QString& owner, const QJsonObject& message, Send send);
    bool complete(const QString& owner, quint64 id, const QJsonValue& result,
                  const QString& error = {});
    void removeOwner(const QString& owner);
  signals:
    void requested(const litecode::extensions::ExternalUriRequest& request);

  private:
    struct Pending {
        Send send;
        bool open{};
    };
    QHash<QString, QHash<quint64, Pending>> pending_;
};
} // namespace litecode::extensions
Q_DECLARE_METATYPE(litecode::extensions::ExternalUriRequest)
