#pragma once
#include "extensions/ProgressApi.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>

namespace litecode::extensions {
struct ProgressTaskInfo final {
    QString owner, source;
    quint64 id{};
    ProgressOptions options;
    std::optional<QString> message;
    std::optional<double> total;
    bool cancellationRequested{};
};
class WorkbenchProgress final : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    bool accept(const QString& owner, const QString& source, const QJsonObject& message);
    bool cancel(const QString& owner, quint64 id);
    void removeOwner(const QString& owner);
    std::optional<ProgressTaskInfo> task(const QString& owner, quint64 id) const;
  signals:
    void started(const litecode::extensions::ProgressTaskInfo& task);
    void changed(const litecode::extensions::ProgressTaskInfo& task);
    void ended(const QString& owner, quint64 id);
    void cancelRequested(const QString& owner, quint64 id);

  private:
    QHash<QString, QHash<quint64, ProgressTaskInfo>> tasks_;
    QHash<QString, quint64> lastStarted_;
};
} // namespace litecode::extensions
Q_DECLARE_METATYPE(litecode::extensions::ProgressTaskInfo)
