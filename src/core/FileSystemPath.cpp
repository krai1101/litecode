#include "core/FileSystemPath.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#ifdef Q_OS_DARWIN
#include <sys/attr.h>
#include <unistd.h>
#endif
#endif

namespace litecode::core {
namespace {

QString nearestExistingPath(QString path) {
    path = QFileInfo(path).absoluteFilePath();
    QFileInfo info(path);
    while (!info.exists()) {
        const QString parent = info.dir().absolutePath();
        if (parent == path)
            break;
        path = parent;
        info.setFile(path);
    }
    return info.absoluteFilePath();
}

std::optional<Qt::CaseSensitivity> probeDirectorySensitivity(const QString& path) {
    const QFileInfo existingInfo(nearestExistingPath(path));
    const QDir directory(existingInfo.isDir() ? existingInfo.absoluteFilePath()
                                              : existingInfo.dir().absolutePath());
    const QFileInfoList entries = directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                          QDir::Hidden | QDir::System);
    for (const QFileInfo& entry : entries) {
        QString alternate = entry.fileName();
        bool changed = false;
        for (qsizetype index = 0; index < alternate.size(); ++index) {
            const QChar character = alternate.at(index);
            if (character >= QLatin1Char('a') && character <= QLatin1Char('z')) {
                alternate[index] = character.toUpper();
                changed = true;
                break;
            }
            if (character >= QLatin1Char('A') && character <= QLatin1Char('Z')) {
                alternate[index] = character.toLower();
                changed = true;
                break;
            }
        }
        if (!changed)
            continue;
        const QString alternatePath = directory.absoluteFilePath(alternate);
        if (!QFileInfo::exists(alternatePath))
            return Qt::CaseSensitive;
        const QByteArray originalIdentity = fileSystemEntryIdentity(entry.absoluteFilePath());
        const QByteArray alternateIdentity = fileSystemEntryIdentity(alternatePath);
        if (!originalIdentity.isEmpty() && !alternateIdentity.isEmpty())
            return originalIdentity == alternateIdentity ? Qt::CaseInsensitive : Qt::CaseSensitive;
    }
    return std::nullopt;
}

} // namespace

Qt::CaseSensitivity fileSystemCaseSensitivity(const QString& path) {
#ifdef Q_OS_WIN
    QString existing = nearestExistingPath(path);
    if (QFileInfo(existing).isFile())
        existing = QFileInfo(existing).dir().absolutePath();
    const std::wstring nativePath = QDir::toNativeSeparators(existing).toStdWString();
    const HANDLE handle = CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        FILE_CASE_SENSITIVE_INFO information{};
        const bool queried =
            GetFileInformationByHandleEx(handle, FileCaseSensitiveInfo, &information,
                                         sizeof(information)) != FALSE;
        CloseHandle(handle);
        if (queried)
            return (information.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) != 0 ? Qt::CaseSensitive
                                                                              : Qt::CaseInsensitive;
    }
    return probeDirectorySensitivity(path).value_or(Qt::CaseSensitive);
#elif defined(Q_OS_DARWIN)
    struct attrlist attributes{};
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.volattr = ATTR_VOL_INFO | ATTR_VOL_CAPABILITIES;
    struct {
        uint32_t length;
        vol_capabilities_attr_t capabilities;
    } result{};
    const QByteArray nativePath = QFile::encodeName(nearestExistingPath(path));
    if (getattrlist(nativePath.constData(), &attributes, &result, sizeof(result), 0) == 0 &&
        result.length >= sizeof(result) &&
        (result.capabilities.valid[VOL_CAPABILITIES_FORMAT] & VOL_CAP_FMT_CASE_SENSITIVE) != 0) {
        return (result.capabilities.capabilities[VOL_CAPABILITIES_FORMAT] &
                VOL_CAP_FMT_CASE_SENSITIVE) != 0
                   ? Qt::CaseSensitive
                   : Qt::CaseInsensitive;
    }
    return probeDirectorySensitivity(path).value_or(Qt::CaseSensitive);
#else
    Q_UNUSED(path);
    return Qt::CaseSensitive;
#endif
}

QString normalizedFileSystemPath(const QString& path) {
    if (path.isEmpty())
        return {};
    QString existing = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList missingSuffix;
    while (!QFileInfo::exists(existing)) {
        const QFileInfo info(existing);
        const QString parent = info.dir().absolutePath();
        if (parent == existing)
            break;
        missingSuffix.prepend(info.fileName());
        existing = parent;
    }
    const QString canonical = QFileInfo(existing).canonicalFilePath();
    QString result = canonical.isEmpty() ? existing : canonical;
    for (const QString& segment : missingSuffix)
        result = QDir(result).filePath(segment);
    return QDir::fromNativeSeparators(QDir::cleanPath(result));
}

QByteArray fileSystemEntryIdentity(const QString& path) {
#ifdef Q_OS_WIN
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const HANDLE handle = CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    FILE_ID_INFO information{};
    const bool success =
        GetFileInformationByHandleEx(handle, FileIdInfo, &information, sizeof(information));
    CloseHandle(handle);
    if (!success)
        return {};
    QByteArray identity;
    QDataStream stream(&identity, QIODevice::WriteOnly);
    stream << static_cast<quint64>(information.VolumeSerialNumber);
    identity.append(reinterpret_cast<const char*>(information.FileId.Identifier),
                    sizeof(information.FileId.Identifier));
    return identity;
#else
    const QByteArray nativePath = QFile::encodeName(path);
    struct stat information{};
    if (stat(nativePath.constData(), &information) != 0)
        return {};
    QByteArray identity;
    QDataStream stream(&identity, QIODevice::WriteOnly);
    stream << static_cast<quint64>(information.st_dev) << static_cast<quint64>(information.st_ino);
    return identity;
#endif
}

bool pathsReferToSameEntry(const QString& left, const QString& right) {
    const QString cleanLeft = normalizedFileSystemPath(left);
    const QString cleanRight = normalizedFileSystemPath(right);
    if (cleanLeft == cleanRight)
        return true;

    const QByteArray leftIdentity = fileSystemEntryIdentity(cleanLeft);
    const QByteArray rightIdentity = fileSystemEntryIdentity(cleanRight);
    if (!leftIdentity.isEmpty() && !rightIdentity.isEmpty())
        return leftIdentity == rightIdentity;

    return cleanLeft.compare(cleanRight, fileSystemCaseSensitivity(cleanLeft)) == 0;
}

} // namespace litecode::core
