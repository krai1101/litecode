#include "extensions/SecretEncryptionBackend.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QPromise>
#include <QQueue>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#ifdef LITECODE_BUNDLED_QTKEYCHAIN
#include <qtkeychain/keychain.h>
#else
#include <qt6keychain/keychain.h>
#endif
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <limits>
#include <optional>
#include <stdexcept>

namespace litecode::extensions {
namespace {

constexpr int KeyBytes = 32, NonceBytes = 12, TagBytes = 16;
const QByteArray Magic("LCSS\x01", 5);

void wipe(QByteArray& bytes) {
    if (!bytes.isEmpty()) OPENSSL_cleanse(bytes.data(), static_cast<size_t>(bytes.size()));
}

void check(int result) {
    if (result != 1) throw std::runtime_error("Authenticated secret encryption failed.");
}

QByteArray crypt(QByteArray data, const QByteArray& context, const QByteArray& key, bool encrypt) {
    const auto clearInput = qScopeGuard([&data, encrypt] { if (encrypt) wipe(data); });
    if (key.size() != KeyBytes || data.size() > std::numeric_limits<int>::max() - 64 ||
        context.size() > std::numeric_limits<int>::max() - Magic.size())
        throw std::runtime_error("Invalid secret encryption input size.");
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!cipher) throw std::runtime_error("Could not allocate secret encryption context.");
    QByteArray nonce(NonceBytes, '\0'), tag(TagBytes, '\0');
    if (encrypt) check(RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), NonceBytes));
    else {
        if (!data.startsWith(Magic) || data.size() < Magic.size() + NonceBytes + TagBytes)
            throw std::runtime_error("Stored secret has an unsupported encrypted format.");
        nonce = data.mid(Magic.size(), NonceBytes);
        tag = data.mid(Magic.size() + NonceBytes, TagBytes);
        data = data.mid(Magic.size() + NonceBytes + TagBytes);
    }
    QByteArray result(data.size() + EVP_MAX_BLOCK_LENGTH, '\0');
    bool delivered = false;
    const auto clearFailedResult = qScopeGuard([&result, &delivered, encrypt] {
        if (!encrypt && !delivered) wipe(result);
    });
    int size{}, tail{};
    check(EVP_CipherInit_ex(cipher.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr, encrypt ? 1 : 0));
    check(EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_SET_IVLEN, NonceBytes, nullptr));
    check(EVP_CipherInit_ex(cipher.get(), nullptr, nullptr,
        reinterpret_cast<const unsigned char*>(key.constData()),
        reinterpret_cast<const unsigned char*>(nonce.constData()), encrypt ? 1 : 0));
    // Authenticate the format and extension identity as well as the payload.
    const auto aad = Magic + context;
    check(EVP_CipherUpdate(cipher.get(), nullptr, &size,
        reinterpret_cast<const unsigned char*>(aad.constData()), static_cast<int>(aad.size())));
    check(EVP_CipherUpdate(cipher.get(), reinterpret_cast<unsigned char*>(result.data()), &size,
        reinterpret_cast<const unsigned char*>(data.constData()), static_cast<int>(data.size())));
    if (!encrypt) check(EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_SET_TAG, TagBytes, tag.data()));
    check(EVP_CipherFinal_ex(cipher.get(), reinterpret_cast<unsigned char*>(result.data()) + size, &tail));
    result.resize(size + tail);
    if (encrypt) {
        check(EVP_CIPHER_CTX_ctrl(cipher.get(), EVP_CTRL_GCM_GET_TAG, TagBytes, tag.data()));
        delivered = true;
        return Magic + nonce + tag + result;
    }
    delivered = true;
    return result;
}

class KeychainSecretEncryption final : public SecretEncryptionBackend {
  public:
    explicit KeychainSecretEncryption(const QString& path) : storagePath_(path), lockPath_(path + ".key-lock") {
        auto identity = QFileInfo(path).canonicalFilePath();
        if (identity.isEmpty()) {
            const QFileInfo file(path);
            auto parent = QFileInfo(file.absolutePath()).canonicalFilePath();
            if (parent.isEmpty()) parent = file.absolutePath();
            identity = parent + '/' + file.fileName();
        }
        account_ = "extension-key-v1-" + QString::fromLatin1(
            QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
        retry_.setSingleShot(true);
        retry_.setInterval(50);
        connect(&retry_, &QTimer::timeout, this, [this] { acquireKey(); });
    }
    ~KeychainSecretEncryption() override {
        retry_.stop();
        if (active_) reject(*active_, "Secret-storage backend stopped.");
        while (!queue_.isEmpty()) { auto item = queue_.dequeue(); reject(item, "Secret-storage backend stopped."); }
        wipe(key_);
    }
    QFuture<QByteArray> encrypt(QByteArray data, QByteArray context) override {
        return enqueue(std::move(data), std::move(context), true);
    }
    QFuture<QByteArray> decrypt(QByteArray data, QByteArray context) override {
        return enqueue(std::move(data), std::move(context), false);
    }

  private:
    struct Operation {
        QByteArray data, context;
        bool encrypt{};
        std::shared_ptr<QPromise<QByteArray>> promise;
    };
    static void reject(Operation& operation, const QString& message) {
        if (operation.encrypt) wipe(operation.data);
        operation.promise->setException(std::make_exception_ptr(std::runtime_error(message.toStdString())));
        operation.promise->finish();
    }
    QFuture<QByteArray> enqueue(QByteArray data, QByteArray context, bool encrypt) {
        auto promise = std::make_shared<QPromise<QByteArray>>();
        promise->start();
        const auto future = promise->future();
        Operation operation{std::move(data), std::move(context), encrypt, promise};
        if (QThread::currentThread() != thread()) {
            reject(operation, "Native keychain requests must run on the extension-host thread.");
            return future;
        }
        queue_.enqueue(std::move(operation));
        if (!active_) next();
        return future;
    }
    void next() {
        if (active_ || queue_.isEmpty()) return;
        active_ = queue_.dequeue();
        if (!key_.isEmpty()) { execute(); return; }
        wait_.start();
        acquireKey();
    }
    void acquireKey() {
        if (!active_) return;
        if (!lock_) lock_ = std::make_unique<QLockFile>(lockPath_);
        if (!lock_->tryLock(0)) {
            if (wait_.elapsed() >= 30000) { finishError("Another window is still initializing secret storage. Retry the operation."); return; }
            retry_.start();
            return;
        }
        auto* job = new QKeychain::ReadPasswordJob("LiteCode", this);
        job->setKey(account_);
        job->setInsecureFallback(false);
        connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job*) {
            if (!active_) return;
            if (job->error() == QKeychain::EntryNotFound) {
                bool existing = false;
                try { existing = hasExistingSecrets(); }
                catch (const std::exception& error) { finishError(QString::fromUtf8(error.what())); return; }
                if (!active_->encrypt || existing) {
                    finishError("The system keychain no longer contains this secret store's encryption key. Existing data was preserved.");
                    return;
                }
                createKey();
            } else if (job->error() != QKeychain::NoError) {
                finishError("System keychain access failed: " + job->errorString());
            } else {
                key_ = job->binaryData();
                if (key_.size() != KeyBytes) { wipe(key_); key_.clear(); finishError("System keychain returned an invalid encryption key."); return; }
                lock_.reset();
                execute();
            }
        });
        job->start();
    }
    void createKey() {
        key_ = QByteArray(KeyBytes, '\0');
        if (RAND_bytes(reinterpret_cast<unsigned char*>(key_.data()), KeyBytes) != 1) {
            wipe(key_); key_.clear(); finishError("Could not generate a secure encryption key."); return;
        }
        auto* job = new QKeychain::WritePasswordJob("LiteCode", this);
        job->setKey(account_);
        job->setBinaryData(key_);
        job->setInsecureFallback(false);
        connect(job, &QKeychain::Job::finished, this, [this, job](QKeychain::Job*) {
            if (job->error() != QKeychain::NoError) {
                wipe(key_); key_.clear(); finishError("Could not save the encryption key in the system keychain: " + job->errorString());
                return;
            }
            lock_.reset();
            execute();
        });
        job->start();
    }
    bool hasExistingSecrets() const {
        QFile file(storagePath_);
        if (!file.exists()) return false;
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("Could not inspect existing secret storage before key creation.");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (file.error() != QFileDevice::NoError || error.error != QJsonParseError::NoError ||
            !document.isObject() || !document.object().value("values").isObject())
            throw std::runtime_error("Existing secret storage is invalid. No replacement encryption key was created.");
        return !document.object().value("values").toObject().isEmpty();
    }
    void execute() {
        if (!active_) return;
        auto operation = std::move(*active_);
        active_.reset();
        try { operation.promise->addResult(crypt(std::move(operation.data), operation.context, key_, operation.encrypt)); }
        catch (...) { operation.promise->setException(std::current_exception()); }
        operation.promise->finish();
        QTimer::singleShot(0, this, [this] { next(); });
    }
    void finishError(const QString& message) {
        lock_.reset();
        if (active_) { reject(*active_, message); active_.reset(); }
        QTimer::singleShot(0, this, [this] { next(); });
    }
    QString account_, storagePath_, lockPath_;
    QByteArray key_;
    QQueue<Operation> queue_;
    std::optional<Operation> active_;
    std::unique_ptr<QLockFile> lock_;
    QElapsedTimer wait_;
    QTimer retry_;
};

} // namespace

std::unique_ptr<SecretEncryptionBackend> createSecretEncryptionBackend(const QString& storagePath) {
    return std::make_unique<KeychainSecretEncryption>(storagePath);
}
void eraseSecretBytes(QByteArray& bytes) { wipe(bytes); }

} // namespace litecode::extensions
