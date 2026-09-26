#include "workspace/WorkspaceService.h"
#include "core/FileSystemPath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace litecode::workspace {
namespace {

#ifdef Q_OS_WIN
QString finalWindowsPath(const QString& path) {
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const HANDLE handle =
        CreateFileW(nativePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (required == 0) {
        CloseHandle(handle);
        return {};
    }
    std::wstring buffer(required, L'\0');
    const DWORD length =
        GetFinalPathNameByHandleW(handle, buffer.data(), required, FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (length == 0 || length >= required) {
        return {};
    }
    QString resolved = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(length));
    if (resolved.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive)) {
        resolved = QStringLiteral("\\\\") + resolved.sliced(8);
    } else if (resolved.startsWith(QStringLiteral("\\\\?\\"))) {
        resolved.remove(0, 4);
    }
    return resolved;
}
#endif

QString resolvedContainmentPath(const QString& path) {
    QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList missingSegments;
    QFileInfo info(candidate);
    while (!info.exists()) {
        missingSegments.prepend(info.fileName());
        const QString parent = info.dir().absolutePath();
        if (parent == candidate) {
            break;
        }
        candidate = parent;
        info.setFile(candidate);
    }
    QString resolved;
#ifdef Q_OS_WIN
    resolved = finalWindowsPath(info.absoluteFilePath());
#endif
    if (resolved.isEmpty()) {
        resolved = info.canonicalFilePath();
    }
    if (resolved.isEmpty()) {
        resolved = info.absoluteFilePath();
    }
    for (const QString& segment : missingSegments) {
        resolved = QDir(resolved).absoluteFilePath(segment);
    }
    return QDir::fromNativeSeparators(QDir::cleanPath(resolved));
}

bool isLinkEntry(const QFileInfo& info) {
    if (info.isSymLink())
        return true;
#ifdef Q_OS_WIN
    return info.isJunction();
#else
    return false;
#endif
}

QString resolvedEntryPath(const QString& path) {
    const QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (!isLinkEntry(info))
        return resolvedContainmentPath(path);
    return QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(resolvedContainmentPath(info.dir().absolutePath()))
                            .absoluteFilePath(info.fileName())));
}

bool pathIsContained(const QString& rootPath, const QString& path, bool followFinalLink) {
    const QString root = resolvedContainmentPath(rootPath);
    const QString candidate =
        followFinalLink ? resolvedContainmentPath(path) : resolvedEntryPath(path);
    const Qt::CaseSensitivity sensitivity = core::fileSystemCaseSensitivity(root);
    return candidate.compare(root, sensitivity) == 0 ||
           candidate.startsWith(root + QLatin1Char('/'), sensitivity);
}

QByteArray identityForPath(const QString& path) {
#ifdef Q_OS_WIN
    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const HANDLE handle =
        CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    FILE_ID_INFO identity{};
    const bool available =
        GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)) != FALSE;
    FILE_BASIC_INFO timestamps{};
    const bool timestampsAvailable =
        GetFileInformationByHandleEx(handle, FileBasicInfo, &timestamps, sizeof(timestamps)) !=
        FALSE;
    CloseHandle(handle);
    if (!available || !timestampsAvailable)
        return {};
    QByteArray result(reinterpret_cast<const char*>(&identity.VolumeSerialNumber),
                      sizeof(identity.VolumeSerialNumber));
    result.append(reinterpret_cast<const char*>(identity.FileId.Identifier),
                  sizeof(identity.FileId.Identifier));
    result.append(reinterpret_cast<const char*>(&timestamps.CreationTime),
                  sizeof(timestamps.CreationTime));
    result.append(reinterpret_cast<const char*>(&timestamps.ChangeTime),
                  sizeof(timestamps.ChangeTime));
    return result;
#else
    const QByteArray nativePath = QFile::encodeName(path);
    struct stat identity{};
    if (::lstat(nativePath.constData(), &identity) != 0)
        return {};
    QByteArray result = QByteArray::number(static_cast<qulonglong>(identity.st_dev)) + ':' +
                        QByteArray::number(static_cast<qulonglong>(identity.st_ino));
    const auto number = [](auto value) {
        return QByteArray::number(static_cast<qlonglong>(value));
    };
#ifdef Q_OS_DARWIN
    result += ':' + number(identity.st_birthtimespec.tv_sec) + ':' +
              number(identity.st_birthtimespec.tv_nsec) + ':' +
              number(identity.st_ctimespec.tv_sec) + ':' + number(identity.st_ctimespec.tv_nsec);
#else
    result += ':' + number(identity.st_ctim.tv_sec) + ':' + number(identity.st_ctim.tv_nsec);
#endif
    result += ':' + number(identity.st_size);
    return result;
#endif
}

void setError(QString* errorMessage, const QString& message) {
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool samePath(const QString& left, const QString& right) {
    const QString resolvedLeft = resolvedEntryPath(left);
    return resolvedLeft.compare(resolvedEntryPath(right),
                                core::fileSystemCaseSensitivity(resolvedLeft)) == 0;
}

WorkspaceNameValidation invalidName(WorkspaceNameIssue issue, const char* message) {
    return {issue, QCoreApplication::translate("WorkspaceService", message)};
}

} // namespace

WorkspaceService::WorkspaceService(QObject* parent)
    : WorkspaceService(
          [](const QString& path, QString* errorMessage) {
              QFile item(path);
              if (item.moveToTrash())
                  return true;
              setError(errorMessage, item.errorString());
              return false;
          },
          parent) {}

WorkspaceService::WorkspaceService(TrashOperation trashOperation, QObject* parent)
    : QObject(parent), trashOperation_(std::move(trashOperation)) {}

QString WorkspaceService::rootPath() const { return rootPath_; }

bool WorkspaceService::hasWorkspace() const { return !rootPath_.isEmpty(); }

bool WorkspaceService::openFolder(const QString& folderPath, QString* errorMessage) {
    const QFileInfo info(folderPath);
    if (!info.exists() || !info.isDir()) {
        setError(errorMessage,
                 tr("The selected workspace folder does not exist or is not a directory."));
        return false;
    }

    QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty()) {
        resolved = QDir::cleanPath(info.absoluteFilePath());
    }
    resolved = QDir::toNativeSeparators(resolved);
    if (!rootPath_.isEmpty() && samePath(resolved, rootPath_)) {
        return true;
    }

    rootPath_ = resolved;
    emit workspaceChanged(rootPath_);
    return true;
}

void WorkspaceService::closeWorkspace() {
    if (rootPath_.isEmpty()) {
        return;
    }
    rootPath_.clear();
    emit workspaceChanged({});
}

bool WorkspaceService::containsPath(const QString& path) const {
    if (rootPath_.isEmpty()) {
        return false;
    }
    return pathIsContained(rootPath_, path, true);
}

bool WorkspaceService::containsEntryPath(const QString& path) const {
    return !rootPath_.isEmpty() && pathIsContained(rootPath_, path, false);
}

QString WorkspaceService::canonicalContainmentPath(const QString& path) {
    return resolvedContainmentPath(path);
}

QByteArray WorkspaceService::entryIdentity(const QString& path) { return identityForPath(path); }

bool WorkspaceService::entryExists(const QString& path) {
    const QFileInfo info(path);
    return info.exists() || isLinkEntry(info);
}

WorkspaceNameValidation WorkspaceService::validateEntryName(const QString& name) {
    if (name.isEmpty() || name.trimmed().isEmpty()) {
        return invalidName(WorkspaceNameIssue::Empty, "Enter a file or folder name.");
    }
    if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
        return invalidName(WorkspaceNameIssue::DotName,
                           "The names '.' and '..' are reserved by the file system.");
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        return invalidName(WorkspaceNameIssue::PathSeparator,
                           "A name cannot contain a path separator.");
    }
    for (const QChar character : name) {
        if (character.unicode() < 0x20) {
            return invalidName(WorkspaceNameIssue::ControlCharacter,
                               "A name cannot contain control characters.");
        }
    }
#ifdef Q_OS_WIN
    if (name.contains(QRegularExpression(QStringLiteral(R"([<>:"|?*])")))) {
        return invalidName(WorkspaceNameIssue::WindowsInvalidCharacter,
                           "Windows names cannot contain <, >, :, \" , |, ? or *.");
    }
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        return invalidName(WorkspaceNameIssue::WindowsTrailingDotOrSpace,
                           "Windows names cannot end with a dot or space.");
    }
    const QString device = name.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QRegularExpression numberedDevice(QStringLiteral(R"(^(?:COM|LPT)[1-9]$)"));
    if (device == QStringLiteral("CON") || device == QStringLiteral("PRN") ||
        device == QStringLiteral("AUX") || device == QStringLiteral("NUL") ||
        device == QStringLiteral("CLOCK$") || device == QStringLiteral("CONIN$") ||
        device == QStringLiteral("CONOUT$") || numberedDevice.match(device).hasMatch()) {
        return invalidName(WorkspaceNameIssue::WindowsReservedDevice,
                           "That name is reserved by Windows. Choose another name.");
    }
#endif
    return {};
}

bool WorkspaceService::createFile(const QString& parentDirectory, const QString& name,
                                  QString* createdPath, QString* errorMessage) {
    const WorkspaceNameValidation validation = validateEntryName(name);
    if (!validation.valid()) {
        setError(errorMessage, validation.message);
        return false;
    }
    if (!containsPath(parentDirectory) || !QFileInfo(parentDirectory).isDir()) {
        setError(errorMessage, tr("Choose a folder inside the workspace."));
        return false;
    }
    const QString path = QDir(parentDirectory).absoluteFilePath(name);
    if (!containsPath(path) || entryExists(path)) {
        setError(errorMessage, tr("That file already exists or is outside the workspace."));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        setError(errorMessage, file.errorString());
        return false;
    }
    file.close();
    if (createdPath) {
        *createdPath = QDir::toNativeSeparators(path);
    }
    return true;
}

bool WorkspaceService::createDirectory(const QString& parentDirectory, const QString& name,
                                       QString* createdPath, QString* errorMessage) {
    const WorkspaceNameValidation validation = validateEntryName(name);
    if (!validation.valid()) {
        setError(errorMessage, validation.message);
        return false;
    }
    if (!containsPath(parentDirectory) || !QFileInfo(parentDirectory).isDir()) {
        setError(errorMessage, tr("Choose a folder inside the workspace."));
        return false;
    }
    const QString path = QDir(parentDirectory).absoluteFilePath(name);
    if (!containsPath(path) || entryExists(path) || !QDir().mkdir(path)) {
        setError(errorMessage, tr("The folder could not be created."));
        return false;
    }
    if (createdPath) {
        *createdPath = QDir::toNativeSeparators(path);
    }
    return true;
}

bool WorkspaceService::renamePath(const QString& path, const QString& newName, QString* renamedPath,
                                  QString* errorMessage) {
    const WorkspaceNameValidation validation = validateEntryName(newName);
    if (!validation.valid()) {
        setError(errorMessage, validation.message);
        return false;
    }
    if (!containsEntryPath(path) || samePath(path, rootPath_) || !entryExists(path)) {
        setError(errorMessage, tr("The selected item cannot be renamed."));
        return false;
    }
    const QString destination = QFileInfo(path).dir().absoluteFilePath(newName);
    const bool destinationExists = entryExists(destination);
    bool caseOnlyRename = false;
#ifndef Q_OS_LINUX
    const QString cleanSource = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString cleanDestination = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    if (destinationExists && cleanSource.compare(cleanDestination, Qt::CaseInsensitive) == 0) {
        const QByteArray sourceIdentity = identityForPath(path);
        caseOnlyRename =
            !sourceIdentity.isEmpty() && identityForPath(destination) == sourceIdentity;
    }
#endif
    if (QDir::cleanPath(path) == QDir::cleanPath(destination)) {
        if (renamedPath)
            *renamedPath = QDir::toNativeSeparators(destination);
        return true;
    }
    if (!containsPath(destination) || (destinationExists && !caseOnlyRename) ||
        !QDir().rename(path, destination)) {
        setError(errorMessage, tr("The item could not be renamed."));
        return false;
    }
    if (renamedPath) {
        *renamedPath = QDir::toNativeSeparators(destination);
    }
    return true;
}

bool WorkspaceService::movePath(const QString& path, const QString& destinationDirectory,
                                QString* movedPath, QString* errorMessage) {
    if (!containsEntryPath(path) || samePath(path, rootPath_) || !entryExists(path) ||
        !containsPath(destinationDirectory) || !QFileInfo(destinationDirectory).isDir()) {
        setError(errorMessage, tr("Both the item and destination must be inside the workspace."));
        return false;
    }
    const QString destination =
        QDir(destinationDirectory).absoluteFilePath(QFileInfo(path).fileName());
    if (samePath(destination, path)) {
        return true;
    }
    if (entryExists(destination) || !QDir().rename(path, destination)) {
        setError(errorMessage, tr("The item could not be moved."));
        return false;
    }
    if (movedPath) {
        *movedPath = QDir::toNativeSeparators(destination);
    }
    return true;
}

bool WorkspaceService::removePath(const QString& path, const QByteArray& expectedIdentity,
                                  QString* errorMessage) {
    if (!containsEntryPath(path) || samePath(path, rootPath_) || !entryExists(path)) {
        setError(errorMessage, tr("The selected item cannot be deleted."));
        return false;
    }
    if (expectedIdentity.isEmpty() || identityForPath(path) != expectedIdentity) {
        setError(errorMessage,
                 tr("The selected item changed after confirmation. Nothing was deleted."));
        return false;
    }
    const QFileInfo info(path);
    bool removed = false;
    if (isLinkEntry(info)) {
        removed = QFile::remove(path);
#ifdef Q_OS_WIN
        // DeleteFile cannot remove a directory reparse point (symbolic link or junction).
        // RemoveDirectory removes the link itself and never follows it into its target.
        if (!removed)
            removed = QDir().rmdir(path);
#endif
    } else {
        removed = info.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    }
    if (!removed) {
        setError(errorMessage, tr("The item could not be deleted."));
    }
    return removed;
}

bool WorkspaceService::movePathToTrash(const QString& path, QString* errorMessage) {
    return movePathToTrash(path, {}, errorMessage);
}

bool WorkspaceService::movePathToTrash(const QString& path, const QByteArray& expectedIdentity,
                                       QString* errorMessage) {
    if (!containsEntryPath(path) || samePath(path, rootPath_) || !entryExists(path)) {
        setError(errorMessage, tr("The selected item cannot be deleted."));
        return false;
    }
    if (!expectedIdentity.isEmpty() && identityForPath(path) != expectedIdentity) {
        setError(errorMessage,
                 tr("The selected item changed after confirmation. Nothing was deleted."));
        return false;
    }
    QString diagnostic;
    if (trashOperation_ && trashOperation_(path, &diagnostic))
        return true;
    setError(errorMessage, diagnostic.isEmpty()
                               ? tr("The item could not be moved to the system trash.")
                               : diagnostic);
    return false;
}

} // namespace litecode::workspace
