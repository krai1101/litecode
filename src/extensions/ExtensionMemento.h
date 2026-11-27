#pragma once

#include "extensions/ExtensionApi.h"
#include <QMutex>

namespace litecode::extensions {

// Host-owned JSON storage. Atomic replacement and a file lock allow separate
// windows to update the same extension's state without losing unrelated keys.
class ExtensionMemento final : public GlobalMemento {
  public:
    void setFilePath(QString path);
    [[nodiscard]] QStringList keys() const override;
    [[nodiscard]] QJsonValue get(const QString& key) const override;
    QFuture<void> update(const QString& key, const QJsonValue& value) override;
    void setKeysForSync(const QStringList& keys) override;
    [[nodiscard]] QJsonObject snapshot() const;

  private:
    [[nodiscard]] QJsonObject read() const;
    void write(const QJsonObject& document) const;
    QString path_;
    QJsonObject transient_;
    mutable QMutex mutex_;
};

} // namespace litecode::extensions
