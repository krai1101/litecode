#pragma once

#include "extensions/ExtensionMemento.h"
#include "extensions/SecretOperationQueue.h"
#include <QFileSystemWatcher>
#include <QObject>

namespace litecode::extensions {
class SecretEncryptionBackend;
class ExtensionSecretStorage final : public QObject, public SecretStorage {
  public:
    ExtensionSecretStorage(const QString& path, QString extensionId);
    ~ExtensionSecretStorage() override;
    QFuture<QStringList> keys() override;
    QFuture<std::optional<QString>> get(const QString& key) override;
    QFuture<void> store(const QString& key, const QString& value) override;
    QFuture<void> delete_(const QString& key) override;
    [[nodiscard]] Event<SecretStorageChangeEvent> onDidChange() const override;

  private:
    void refresh();
    QString extensionId_;
    ExtensionMemento encrypted_;
    std::unique_ptr<SecretEncryptionBackend> encryption_;
    QFileSystemWatcher watcher_;
    QJsonObject previous_;
    QMutex mutex_;
    EventEmitter<SecretStorageChangeEvent> changed_;
    SecretOperationQueue operations_{this};
};
} // namespace litecode::extensions
