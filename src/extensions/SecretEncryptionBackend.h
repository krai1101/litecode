#pragma once

#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QString>
#include <memory>

namespace litecode::extensions {

// Private OS encryption adapter. The public SecretStorage API and ciphertext
// inventory remain platform-independent. No secret is stored as plaintext on
// disk when the system credential service is unavailable or denies access.
class SecretEncryptionBackend : public QObject {
  public:
    using QObject::QObject;
    virtual QFuture<QByteArray> encrypt(QByteArray plaintext, QByteArray context) = 0;
    virtual QFuture<QByteArray> decrypt(QByteArray ciphertext, QByteArray context) = 0;
};

[[nodiscard]] std::unique_ptr<SecretEncryptionBackend> createSecretEncryptionBackend(
    const QString& storagePath);
void eraseSecretBytes(QByteArray& bytes);

} // namespace litecode::extensions
