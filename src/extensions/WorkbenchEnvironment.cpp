#include "extensions/WorkbenchEnvironment.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSysInfo>
#include <QUuid>
#include <stdexcept>

namespace litecode::extensions {
namespace {
QString processSessionId() {
    static const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return id;
}
} // namespace
WorkbenchEnvironment::WorkbenchEnvironment(const QString& identityFile) {
    info_.appName = QCoreApplication::applicationName();
    info_.appRoot = QCoreApplication::applicationDirPath();
    info_.appHost = "desktop";
    // LiteCode currently ships an English UI, no portable-data mode or remote
    // extension host. Report actual product state, not the host OS's locale.
    info_.language = "en";
    info_.sessionId = processSessionId();
    try {
        const auto path = QFileInfo(identityFile).absoluteFilePath();
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) throw std::runtime_error("Could not create environment identity directory.");
        QLockFile lock(path + ".lock");
        if (!lock.tryLock(1000)) throw std::runtime_error("Environment identity is locked by another editor process.");
        if (QFile::exists(path)) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) throw std::runtime_error("Could not read environment identity.");
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) throw std::runtime_error("Invalid environment identity file.");
            const auto object = document.object();
            info_.machineId = object.value("machineId").toString();
            info_.firstSessionDate = QDateTime::fromString(object.value("firstSessionDate").toString(), Qt::ISODateWithMs);
            if (info_.machineId.isEmpty() || !info_.firstSessionDate.isValid()) throw std::runtime_error("Invalid environment identity values.");
        } else {
            const auto machine = QSysInfo::machineUniqueId();
            info_.machineId = machine.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                : QString::fromLatin1(QCryptographicHash::hash(QByteArray("LiteCode:") + machine, QCryptographicHash::Sha256).toHex());
            info_.firstSessionDate = QDateTime::currentDateTimeUtc();
            const auto bytes = QJsonDocument(QJsonObject{{"machineId", info_.machineId},
                {"firstSessionDate", info_.firstSessionDate.toString(Qt::ISODateWithMs)}}).toJson(QJsonDocument::Compact);
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                throw std::runtime_error("Could not persist environment identity.");
        }
    } catch (const std::exception& exception) { error_ = QString::fromUtf8(exception.what()); }
}
} // namespace litecode::extensions
