#include "extensions/ExtensionMemento.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutexLocker>
#include <QPromise>
#include <QSaveFile>
#include <exception>
#include <stdexcept>

namespace litecode::extensions {
namespace {
[[noreturn]] void storageError(const QString& message) {
    throw std::runtime_error(message.toStdString());
}
QJsonObject emptyState() {
    return {{"version", 1}, {"values", QJsonObject{}}, {"syncKeys", QJsonArray{}}};
}
} // namespace

void ExtensionMemento::setFilePath(QString path) {
    QMutexLocker lock(&mutex_);
    path_ = std::move(path);
    transient_ = emptyState();
}

QJsonObject ExtensionMemento::read() const {
    if (path_.isEmpty()) return transient_.isEmpty() ? emptyState() : transient_;
    QFile file(path_);
    if (!file.exists()) return emptyState();
    if (!file.open(QIODevice::ReadOnly)) storageError(file.errorString());
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value("version").toInt() != 1 ||
        !document.object().value("values").isObject() ||
        !document.object().value("syncKeys").isArray())
        storageError("Extension state is invalid: " + path_);
    return document.object();
}

void ExtensionMemento::write(const QJsonObject& document) const {
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) storageError(file.errorString());
    const auto encoded = QJsonDocument(document).toJson(QJsonDocument::Compact);
    if (file.write(encoded) != encoded.size() || !file.commit())
        storageError(file.errorString());
}

QStringList ExtensionMemento::keys() const {
    QMutexLocker lock(&mutex_);
    return read().value("values").toObject().keys();
}

QJsonObject ExtensionMemento::snapshot() const {
    QMutexLocker lock(&mutex_);
    return read().value("values").toObject();
}

QJsonValue ExtensionMemento::get(const QString& key) const {
    QMutexLocker lock(&mutex_);
    return read().value("values").toObject().value(key);
}

QFuture<void> ExtensionMemento::update(const QString& key, const QJsonValue& value) {
    QPromise<void> promise;
    promise.start();
    const auto future = promise.future();
    try {
        QMutexLocker lock(&mutex_);
        std::unique_ptr<QLockFile> fileLock;
        if (!path_.isEmpty()) {
            if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
                storageError("Could not create extension state directory.");
            fileLock = std::make_unique<QLockFile>(path_ + ".lock");
            if (!fileLock->tryLock(5000)) storageError("Extension state is locked.");
        }
        auto document = read();
        auto values = document.value("values").toObject();
        if (value.isUndefined()) values.remove(key);
        else values.insert(key, value);
        document.insert("values", values);
        if (path_.isEmpty()) transient_ = document;
        else write(document);
    } catch (...) {
        promise.setException(std::current_exception());
    }
    promise.finish();
    return future;
}

void ExtensionMemento::setKeysForSync(const QStringList& keys) {
    QMutexLocker lock(&mutex_);
    auto uniqueKeys = keys;
    uniqueKeys.removeDuplicates();
    std::unique_ptr<QLockFile> fileLock;
    if (!path_.isEmpty()) {
        if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
            storageError("Could not create extension state directory.");
        fileLock = std::make_unique<QLockFile>(path_ + ".lock");
        if (!fileLock->tryLock(5000)) storageError("Extension state is locked.");
    }
    auto document = read();
    document.insert("syncKeys", QJsonArray::fromStringList(uniqueKeys));
    if (path_.isEmpty()) transient_ = document;
    else write(document);
}
} // namespace litecode::extensions
