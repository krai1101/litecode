#pragma once
#include "extensions/DialogApi.h"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <functional>
#include <variant>

namespace litecode::extensions {
struct FileDialogRequest final {
    QString owner;
    quint64 id{};
    std::variant<OpenDialogOptions, SaveDialogOptions> options;
};
class WorkbenchDialogs final : public QObject {
    Q_OBJECT
  public:
    using Reply = std::function<void(std::optional<QVector<QUrl>>, const QString&)>;
    using QObject::QObject;
    bool accept(const QString& owner, const QJsonObject& message, Reply reply);
    bool complete(const QString& owner, quint64 id, std::optional<QVector<QUrl>> selection, const QString& error = {});
    void removeOwner(const QString& owner);
  signals:
    void requested(const litecode::extensions::FileDialogRequest& request);
    void dismissed(const QString& owner, quint64 id);
  private:
    struct Pending { FileDialogRequest request; Reply reply; };
    QHash<QString, QHash<quint64, Pending>> pending_;
};
} // namespace litecode::extensions
Q_DECLARE_METATYPE(litecode::extensions::FileDialogRequest)
