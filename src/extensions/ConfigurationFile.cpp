#include "extensions/ConfigurationFile.h"
#include "core/JsoncDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMutexLocker>
#include <QSaveFile>
#include <stdexcept>

namespace litecode::extensions {
namespace {

[[noreturn]] void fail(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

core::JsoncDocument readDocument(const QString& path) {
    QFile file(path);
    if (!file.exists()) return core::JsoncDocument::parse("{}");
    if (!file.open(QIODevice::ReadOnly))
        fail("Could not read configuration " + path + ": " + file.errorString());
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        fail("Could not read configuration " + path + ": " + file.errorString());
    auto document = core::JsoncDocument::parse(bytes, true);
    if (!document.value().isObject()) fail("Expected a configuration object in " + path);
    return document;
}

QJsonValue atPath(QJsonValue value, const QStringList& path) {
    for (const auto& key : path) {
        if (!value.isObject()) fail("Configuration settings parent is not an object.");
        value = value.toObject().value(key);
        if (value.isUndefined()) return value;
    }
    return value;
}

} // namespace

ConfigurationFile::ConfigurationFile(QString path, QStringList settingsPath)
    : path_(QFileInfo(path).absoluteFilePath()), settingsPath_(std::move(settingsPath)) {
    if (path.trimmed().isEmpty()) throw std::invalid_argument("Configuration path is empty.");
}

ConfigurationLayer ConfigurationFile::read() const {
    QMutexLocker lock(&mutex_);
    const auto document = readDocument(path_);
    const auto value = atPath(document.value(), settingsPath_);
    if (value.isUndefined()) return {};
    if (!value.isObject()) fail("Expected settings object in " + path_);
    const auto settings = value.toObject();
    ConfigurationLayer result;
    // Use the syntax tree's document order for override groups; QJsonObject
    // iteration sorts keys and would silently change combined-group priority.
    auto names = document.propertyNames(settingsPath_);
    names.removeDuplicates();
    for (const auto& key : names) {
        const auto setting = settings.value(key);
        result.addSetting(key, setting);
        const auto identifiers = configurationOverrideIdentifiers(key);
        if (!identifiers.isEmpty() && setting.isObject())
            result.addOverride(identifiers, setting.toObject());
    }
    return result;
}

bool ConfigurationFile::update(const QString& key, const QJsonValue& value,
                                const std::optional<QString>& languageId) {
    if (key.isEmpty() || key.split('.').contains(QString{}))
        throw std::invalid_argument("Invalid configuration key.");
    if (languageId && (languageId->isEmpty() || *languageId != languageId->trimmed() ||
                       languageId->contains('[') || languageId->contains(']')))
        throw std::invalid_argument("Invalid language override identifier.");
    QMutexLocker guard(&mutex_);
    const auto parent = QFileInfo(path_).absolutePath();
    if (!QDir().mkpath(parent)) fail("Could not create settings directory " + parent);
    // Canonical paths make two windows opening the same file through aliases
    // contend on the same lock rather than losing each other's updates.
    auto target = QFileInfo(path_).canonicalFilePath();
    if (target.isEmpty()) {
        const auto canonicalParent = QFileInfo(parent).canonicalFilePath();
        if (canonicalParent.isEmpty()) fail("Could not resolve settings directory " + parent);
        target = QDir(canonicalParent).filePath(QFileInfo(path_).fileName());
    }
    QLockFile fileLock(target + ".litecode-settings-lock");
    if (!fileLock.tryLock(5000)) fail("Configuration is locked by another writer: " + path_);
    const bool initiallyExists = QFile::exists(target);
    const auto document = readDocument(target);
    // Validate the settings section even for a removal of a missing key.
    const auto settings = atPath(document.value(), settingsPath_);
    if (!settings.isUndefined() && !settings.isObject())
        fail("Expected settings object in " + path_);
    auto propertyPath = settingsPath_;
    if (languageId) propertyPath.append('[' + *languageId + ']');
    propertyPath.append(key);
    const auto updated = document.updateProperty(propertyPath, value);
    QFile original(target);
    QByteArray originalBytes;
    if (original.exists()) {
        if (!original.open(QIODevice::ReadOnly)) fail("Could not reread settings file " + path_);
        originalBytes = original.readAll();
        if (original.error() != QFileDevice::NoError) fail("Could not reread settings file " + path_);
        original.close();
    } else if (initiallyExists) {
        fail("Settings file was removed during the update: " + path_);
    } else if (value.isUndefined()) return false;
    if (initiallyExists && originalBytes != document.source())
        fail("Settings file changed during the update. Retry using its latest contents: " + path_);
    if (!initiallyExists && original.exists())
        fail("Settings file was created by another writer during the update: " + path_);
    if (originalBytes == updated) return false;
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(updated) != updated.size() || !output.commit())
        fail("Could not save configuration " + path_ + ": " + output.errorString());
    return true;
}

} // namespace litecode::extensions
