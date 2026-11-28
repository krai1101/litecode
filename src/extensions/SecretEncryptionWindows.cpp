#include "extensions/SecretEncryptionBackend.h"

#include <QPromise>
#include <QScopeGuard>
#include <limits>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dpapi.h>

namespace litecode::extensions {
namespace {

QByteArray protect(QByteArray bytes, const QByteArray& entropy, bool encrypt) {
    const auto clearInput = qScopeGuard([&bytes, encrypt] {
        if (encrypt && !bytes.isEmpty())
            SecureZeroMemory(bytes.data(), bytes.size());
    });
    if (static_cast<quint64>(bytes.size()) > std::numeric_limits<DWORD>::max() ||
        static_cast<quint64>(entropy.size()) > std::numeric_limits<DWORD>::max())
        throw std::runtime_error("Secret exceeds the platform storage size.");
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB scope{static_cast<DWORD>(entropy.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB output{};
    const auto success =
        encrypt ? CryptProtectData(&input, L"LiteCode extension secret", &scope, nullptr, nullptr,
                                   CRYPTPROTECT_UI_FORBIDDEN, &output)
                : CryptUnprotectData(&input, nullptr, &scope, nullptr, nullptr,
                                     CRYPTPROTECT_UI_FORBIDDEN, &output);
    const DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (!success)
        throw std::runtime_error("DPAPI secret storage failed (code " + std::to_string(error) +
                                 ").");
    const auto release = qScopeGuard([&output] {
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
    });
    return QByteArray(reinterpret_cast<const char*>(output.pbData), output.cbData);
}

class WindowsSecretEncryption final : public SecretEncryptionBackend {
  public:
    QFuture<QByteArray> encrypt(QByteArray data, QByteArray context) override {
        return operation(std::move(data), context, true);
    }
    QFuture<QByteArray> decrypt(QByteArray data, QByteArray context) override {
        return operation(std::move(data), context, false);
    }

  private:
    static QFuture<QByteArray> operation(QByteArray data, const QByteArray& context, bool encrypt) {
        QPromise<QByteArray> promise;
        promise.start();
        const auto future = promise.future();
        try {
            promise.addResult(protect(std::move(data), context, encrypt));
        } catch (...) {
            promise.setException(std::current_exception());
        }
        promise.finish();
        return future;
    }
};

} // namespace

std::unique_ptr<SecretEncryptionBackend> createSecretEncryptionBackend(const QString&) {
    return std::make_unique<WindowsSecretEncryption>();
}
void eraseSecretBytes(QByteArray& bytes) {
    if (!bytes.isEmpty())
        SecureZeroMemory(bytes.data(), bytes.size());
}

} // namespace litecode::extensions
