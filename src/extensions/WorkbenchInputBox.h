#pragma once
#include "extensions/InputBoxApi.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
namespace litecode::extensions {
struct InputBoxRequest final {
    QString owner;
    quint64 id{};
    InputBoxOptions options;
    bool hasValidation{};
};
class WorkbenchInputBox final : public QObject {
    Q_OBJECT
  public:
    using Send = std::function<void(const QJsonObject&)>;
    using QObject::QObject;
    bool accept(const QString& owner, const QJsonObject& message, Send send);
    quint64 validate(const QString& owner, quint64 id, const QString& value);
    bool complete(const QString& owner, quint64 id, std::optional<QString> value,
                  const QString& error = {});
    void removeOwner(const QString& owner);
    [[nodiscard]] bool isPending(const QString& owner, quint64 id) const {
        const auto requests = pending_.constFind(owner);
        return requests != pending_.cend() && requests->contains(id);
    }
  signals:
    void requested(const litecode::extensions::InputBoxRequest& request);
    void dismissed(const QString& owner, quint64 id);
    void validated(const QString& owner, quint64 id, quint64 sequence,
                   const litecode::extensions::InputBoxValidationResult& result,
                   const QString& error);

  private:
    struct Pending {
        InputBoxRequest request;
        Send send;
        quint64 nextSequence{};
        QSet<quint64> validations;
    };
    QHash<QString, QHash<quint64, Pending>> pending_;
};
} // namespace litecode::extensions
Q_DECLARE_METATYPE(litecode::extensions::InputBoxRequest)
Q_DECLARE_METATYPE(litecode::extensions::InputBoxValidationResult)
