#include "extensions/ExtensionSecretStorage.h"
#include "extensions/SecretEncryptionBackend.h"
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QPromise>
#include <QSet>
#include <QScopeGuard>
#include <QThread>
#include <exception>
#include <stdexcept>
#include <type_traits>

namespace litecode::extensions {
namespace {
template<typename T, typename F> QFuture<T> completed(F&& operation) {
    QPromise<T> promise;
    promise.start();
    const auto future = promise.future();
    try {
        if constexpr (std::is_void_v<T>) operation();
        else promise.addResult(operation());
    } catch (...) {
        promise.setException(std::current_exception());
    }
    promise.finish();
    return future;
}

} // namespace

ExtensionSecretStorage::ExtensionSecretStorage(const QString& path, QString extensionId)
    : extensionId_(std::move(extensionId)) {
    const auto parent = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(parent))
        throw std::runtime_error("Could not create secret-storage directory.");
    encrypted_.setFilePath(path);
    encryption_ = createSecretEncryptionBackend(path);
    previous_ = encrypted_.snapshot();
    // Watch the parent because QSaveFile atomically replaces the target inode.
    if (!watcher_.addPath(parent))
        throw std::runtime_error("Could not watch secret-storage changes.");
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
        [this](const QString&) {
            try { refresh(); } catch (...) {
                // Access methods surface storage errors through their futures.
            }
        });
}
ExtensionSecretStorage::~ExtensionSecretStorage() { operations_.shutdown(); }

QFuture<QStringList> ExtensionSecretStorage::keys() {
    return operations_.enqueue<QStringList>(QString{}, [this] {
        return completed<QStringList>([this] { return encrypted_.keys(); });
    });
}

QFuture<std::optional<QString>> ExtensionSecretStorage::get(const QString& key) {
    return operations_.enqueue<std::optional<QString>>(key, [this, key] {
        const auto value = encrypted_.get(key);
        if (value.isUndefined()) return completed<std::optional<QString>>([] { return std::optional<QString>{}; });
        if (!value.isString()) throw std::runtime_error("Stored secret has an invalid format.");
        const auto cipher = QByteArray::fromBase64(value.toString().toLatin1(),
                                                  QByteArray::AbortOnBase64DecodingErrors);
        if (cipher.isEmpty()) throw std::runtime_error("Stored secret has an invalid encrypted value.");
        return encryption_->decrypt(cipher, extensionId_.toUtf8()).then(this,
            [](QByteArray plaintext) -> std::optional<QString> {
                const auto clear = qScopeGuard([&plaintext] { eraseSecretBytes(plaintext); });
                return QString::fromUtf8(plaintext);
            });
    });
}

QFuture<void> ExtensionSecretStorage::store(const QString& key, const QString& value) {
    return operations_.enqueue<void>(key, [this, key, value] {
        return encryption_->encrypt(value.toUtf8(), extensionId_.toUtf8()).then(this,
            [this, key](const QByteArray& cipher) {
                encrypted_.update(key, QString::fromLatin1(cipher.toBase64())).waitForFinished();
                refresh();
            });
    });
}

QFuture<void> ExtensionSecretStorage::delete_(const QString& key) {
    return operations_.enqueue<void>(key, [this, key] {
        return completed<void>([this, key] {
            encrypted_.update(key, QJsonValue(QJsonValue::Undefined)).waitForFinished();
            refresh();
        });
    });
}

Event<SecretStorageChangeEvent> ExtensionSecretStorage::onDidChange() const {
    return changed_.event();
}

void ExtensionSecretStorage::refresh() {
    QStringList changedKeys;
    {
        QMutexLocker lock(&mutex_);
        const auto current = encrypted_.snapshot();
        QSet<QString> all;
        for (auto it = previous_.begin(); it != previous_.end(); ++it) all.insert(it.key());
        for (auto it = current.begin(); it != current.end(); ++it) all.insert(it.key());
        for (const auto& key : all)
            if (previous_.value(key) != current.value(key)) changedKeys.append(key);
        previous_ = current;
    }
    changedKeys.sort();
    if (QThread::currentThread() == thread()) {
        for (const auto& key : changedKeys) changed_.fire({key});
    } else {
        QMetaObject::invokeMethod(this, [this, changedKeys] {
            for (const auto& key : changedKeys) changed_.fire({key});
        }, Qt::QueuedConnection);
    }
}
} // namespace litecode::extensions
