#include "extensions/LocalFileSystem.h"
#include <QtConcurrentRun>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <filesystem>

namespace litecode::extensions {
namespace {
namespace fs = std::filesystem;
QString path(const QUrl& uri) {
    if (!uri.isValid() || !uri.isLocalFile() || uri.hasQuery() || uri.hasFragment())
        throw FileSystemError::Unavailable("No filesystem provider for " + uri.toDisplayString());
    const auto local = uri.toLocalFile();
    if (local.isEmpty() || !QFileInfo(local).isAbsolute() || local.contains(QChar::Null))
        throw FileSystemError::Unavailable("A file URI must contain an absolute path.");
    return QDir::cleanPath(local);
}
fs::path native(const QString& value) {
#ifdef Q_OS_WIN
    return fs::path(value.toStdWString());
#else
    const auto bytes = QFile::encodeName(value);
    return fs::path(bytes.constData());
#endif
}
QString decoded(const fs::path& value) {
#ifdef Q_OS_WIN
    return QString::fromStdWString(value.wstring());
#else
    return QFile::decodeName(value.native().c_str());
#endif
}
[[noreturn]] void fail(const std::error_code& error, const QString& resource) {
    const auto message = resource + ": " + QString::fromStdString(error.message());
    if (error == std::errc::no_such_file_or_directory) throw FileSystemError::FileNotFound(message);
    if (error == std::errc::file_exists) throw FileSystemError::FileExists(message);
    if (error == std::errc::not_a_directory) throw FileSystemError::FileNotADirectory(message);
    if (error == std::errc::is_a_directory) throw FileSystemError::FileIsADirectory(message);
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted)
        throw FileSystemError::NoPermissions(message);
    throw FileSystemError::Unavailable(message);
}
void check(const std::error_code& error, const QString& resource) { if (error) fail(error, resource); }
[[noreturn]] void fileError(const QFileDevice& file) {
    if (file.error() == QFileDevice::PermissionsError) throw FileSystemError::NoPermissions(file.errorString());
    throw FileSystemError::Unavailable(file.errorString());
}
fs::file_status status(const QString& value, bool follow = true) {
    std::error_code error;
    auto result = follow ? fs::status(native(value), error) : fs::symlink_status(native(value), error);
    if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory) fail(error, value);
    return result;
}
bool exists(const QString& value) { return fs::exists(status(value, false)); }
void requireExists(const QString& value) { if (!exists(value)) throw FileSystemError::FileNotFound(value); }
QString writeTarget(QString value) {
    QSet<QString> visited;
    for (int links = 0; fs::is_symlink(status(value, false)); ++links) {
        if (links >= 40 || visited.contains(value)) throw FileSystemError::Unavailable("Symbolic-link cycle: " + value);
        visited.insert(value);
        std::error_code error;
        auto target = fs::read_symlink(native(value), error);
        check(error, value);
        if (target.is_relative()) target = native(value).parent_path() / target;
        value = QDir::cleanPath(decoded(target));
    }
    return value;
}
void requireDirectory(const QString& value) {
    requireExists(value);
    if (!fs::is_directory(status(value))) throw FileSystemError::FileNotADirectory(value);
}
void mkdir(const QString& value) {
    if (exists(value)) {
        if (!fs::is_directory(status(value))) throw FileSystemError::FileExists(value);
        return;
    }
    std::error_code error;
    fs::create_directories(native(value), error);
    check(error, value);
}
FileType type(const QString& value) {
    const auto link = status(value, false);
    auto target = fs::is_symlink(link) ? status(value) : link;
    auto result = fs::is_regular_file(target) ? FileType::File : fs::is_directory(target) ? FileType::Directory : FileType::Unknown;
    if (fs::is_symlink(link)) result = result | FileType::SymbolicLink;
    return result;
}
void remove(const QString& value, bool recursive) {
    requireExists(value);
    std::error_code error;
    // remove_all operates on links themselves, never their directory targets.
    if (recursive) fs::remove_all(native(value), error);
    else fs::remove(native(value), error);
    check(error, value);
}
bool same(const QString& first, const QString& second) {
    if (first == second) return true;
    std::error_code error;
    return fs::equivalent(native(first), native(second), error) && !error;
}
void guardDescendant(const QString& source, const QString& target) {
    std::error_code error;
    const auto from = fs::weakly_canonical(native(source), error);
    check(error, source);
    const auto to = fs::weakly_canonical(native(target), error);
    check(error, target);
    auto a = from.begin(), b = to.begin();
    for (; a != from.end() && b != to.end() && *a == *b; ++a, ++b) {}
    if (a == from.end()) throw FileSystemError::Unavailable("Destination is inside the source: " + target);
}
void move(const QString& source, const QString& target) {
    std::error_code error;
    fs::rename(native(source), native(target), error);
    check(error, source + " -> " + target);
}
// Retain the old destination until replacement succeeds. Failure recovery
// preserves its backup rather than deleting data after a failed rollback.
void replace(const QString& source, const QString& target, bool overwrite) {
    if (exists(target) && !overwrite) throw FileSystemError::FileExists(target);
    mkdir(QFileInfo(target).absolutePath());
    if (!exists(target)) { move(source, target); return; }
    QTemporaryDir backup(QFileInfo(target).absolutePath() + "/.litecode-fs-backup-XXXXXX");
    if (!backup.isValid()) throw FileSystemError::Unavailable("Cannot create replacement backup.");
    const auto retained = backup.filePath("entry");
    move(target, retained);
    try { move(source, target); }
    catch (...) {
        try { move(retained, target); }
        catch (...) {
            backup.setAutoRemove(false);
            throw FileSystemError::Unavailable("Replacement and rollback failed; original destination is preserved at " + retained);
        }
        throw;
    }
}
} // namespace

LocalFileSystem::LocalFileSystem() { workers_.setMaxThreadCount(1); }
QFuture<FileStat> LocalFileSystem::stat(const QUrl& uri) {
    return QtConcurrent::run(&workers_, [uri] {
        const auto value = path(uri);
        requireExists(value);
        const QFileInfo info(value);
        // VS Code's disk provider deliberately exposes birth time, not the
        // POSIX inode-change timestamp, through the public ctime member.
        const auto created = info.birthTime();
        FileStat result{type(value), created.isValid() ? created.toMSecsSinceEpoch() : 0,
            info.lastModified().toMSecsSinceEpoch(), info.size(), {}};
        if ((status(value).permissions() & fs::perms::owner_write) == fs::perms::none)
            result.permissions = FilePermission::Readonly;
        return result;
    });
}
QFuture<QVector<std::pair<QString, FileType>>> LocalFileSystem::readDirectory(const QUrl& uri) {
    return QtConcurrent::run(&workers_, [uri] {
        const auto value = path(uri);
        requireDirectory(value);
        QVector<std::pair<QString, FileType>> result;
        std::error_code error;
        fs::directory_iterator iterator(native(value), error), end;
        check(error, value);
        for (; iterator != end; iterator.increment(error)) {
            check(error, value);
            const auto child = decoded(iterator->path());
            // A disappearing or inaccessible individual child does not fail
            // enumeration of an otherwise readable directory upstream.
            try { result.append({QFileInfo(child).fileName(), type(child)}); }
            catch (const FileSystemError&) {}
        }
        check(error, value);
        return result;
    });
}
QFuture<void> LocalFileSystem::createDirectory(const QUrl& uri) {
    return QtConcurrent::run(&workers_, [uri] { mkdir(path(uri)); });
}
QFuture<QByteArray> LocalFileSystem::readFile(const QUrl& uri) {
    return QtConcurrent::run(&workers_, [uri] {
        const auto value = path(uri);
        requireExists(value);
        if (!fs::exists(status(value))) throw FileSystemError::FileNotFound(uri);
        if (fs::is_directory(status(value))) throw FileSystemError::FileIsADirectory(uri);
        QFile file(value);
        if (!file.open(QIODevice::ReadOnly)) fileError(file);
        auto data = file.readAll();
        if (file.error() != QFileDevice::NoError) fileError(file);
        return data;
    });
}
QFuture<void> LocalFileSystem::writeFile(const QUrl& uri, const QByteArray& content) {
    return QtConcurrent::run(&workers_, [uri, content] {
        auto value = path(uri);
        if (fs::is_directory(status(value))) throw FileSystemError::FileIsADirectory(uri);
        // Follow the final link, including a not-yet-created referent. Atomic
        // replacement must update the referent, never erase the symbolic link.
        value = writeTarget(value);
        mkdir(QFileInfo(value).absolutePath());
        QSaveFile file(value);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) fileError(file);
        if (file.write(content) != content.size() || !file.commit()) fileError(file);
    });
}
QFuture<void> LocalFileSystem::delete_(const QUrl& uri, FileSystemDeleteOptions options) {
    return QtConcurrent::run(&workers_, [uri, options] {
        const auto value = path(uri);
        requireExists(value);
        if (!options.recursive && fs::is_directory(status(value, false))) {
            std::error_code error;
            const bool empty = fs::is_empty(native(value), error);
            check(error, value);
            if (!empty) throw FileSystemError::Unavailable("Directory is not empty: " + value);
        }
        if (options.useTrash) {
            if (!QFile::supportsMoveToTrash())
                throw FileSystemError::Unavailable("This platform cannot move resources to the system trash.");
            if (!QFile::moveToTrash(value)) throw FileSystemError::Unavailable("Could not move resource to the system trash: " + value);
        } else remove(value, options.recursive);
    });
}
QFuture<void> LocalFileSystem::rename(const QUrl& source, const QUrl& target, FileSystemOverwriteOptions options) {
    return QtConcurrent::run(&workers_, [source, target, options] {
        const auto from = path(source), to = path(target);
        requireExists(from);
        if (from == to) return;
        if (same(from, to)) { move(from, to); return; }
        if (fs::is_directory(status(from, false))) guardDescendant(from, to);
        replace(from, to, options.overwrite);
    });
}
QFuture<void> LocalFileSystem::copy(const QUrl& source, const QUrl& target, FileSystemOverwriteOptions options) {
    return QtConcurrent::run(&workers_, [source, target, options] {
        const auto from = path(source), to = path(target);
        requireExists(from);
        if (from == to) return;
        if (same(from, to)) throw FileSystemError::FileExists(target);
        if (fs::is_directory(status(from, false))) guardDescendant(from, to);
        if (exists(to) && !options.overwrite) throw FileSystemError::FileExists(target);
        mkdir(QFileInfo(to).absolutePath());
        QTemporaryDir staged(QFileInfo(to).absolutePath() + "/.litecode-fs-copy-XXXXXX");
        if (!staged.isValid()) throw FileSystemError::Unavailable("Cannot stage resource copy.");
        const auto entry = staged.filePath("entry");
        std::error_code error;
        fs::copy(native(from), native(entry), fs::copy_options::recursive | fs::copy_options::copy_symlinks, error);
        check(error, from);
        replace(entry, to, options.overwrite);
    });
}
std::optional<bool> LocalFileSystem::isWritableFileSystem(const QString& scheme) const {
    return scheme == "file" ? std::optional<bool>(true) : std::nullopt;
}
} // namespace litecode::extensions
