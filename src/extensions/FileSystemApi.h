#pragma once
#include <QByteArray>
#include <QFuture>
#include <QUrl>
#include <QVector>
#include <optional>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
enum class FileType { Unknown = 0, File = 1, Directory = 2, SymbolicLink = 64 };
constexpr FileType operator|(FileType left, FileType right) {
    return static_cast<FileType>(static_cast<int>(left) | static_cast<int>(right));
}
enum class FilePermission { Readonly = 1 };
struct FileStat final {
    FileType type{FileType::Unknown};
    qint64 ctime{}, mtime{}, size{};
    std::optional<FilePermission> permissions;
};
class FileSystemError final : public std::runtime_error {
  public:
    explicit FileSystemError(const QString& message = {}) : FileSystemError("Unknown", message) {}
    explicit FileSystemError(const QUrl& uri) : FileSystemError(uri.toDisplayString()) {}
    const QString code;
    static FileSystemError FileNotFound(const QString& message = {}) {
        return {"FileNotFound", message};
    }
    static FileSystemError FileNotFound(const QUrl& uri) {
        return FileNotFound(uri.toDisplayString());
    }
    static FileSystemError FileExists(const QString& message = {}) {
        return {"FileExists", message};
    }
    static FileSystemError FileExists(const QUrl& uri) { return FileExists(uri.toDisplayString()); }
    static FileSystemError FileNotADirectory(const QString& message = {}) {
        return {"FileNotADirectory", message};
    }
    static FileSystemError FileNotADirectory(const QUrl& uri) {
        return FileNotADirectory(uri.toDisplayString());
    }
    static FileSystemError FileIsADirectory(const QString& message = {}) {
        return {"FileIsADirectory", message};
    }
    static FileSystemError FileIsADirectory(const QUrl& uri) {
        return FileIsADirectory(uri.toDisplayString());
    }
    static FileSystemError NoPermissions(const QString& message = {}) {
        return {"NoPermissions", message};
    }
    static FileSystemError NoPermissions(const QUrl& uri) {
        return NoPermissions(uri.toDisplayString());
    }
    static FileSystemError Unavailable(const QString& message = {}) {
        return {"Unavailable", message};
    }
    static FileSystemError Unavailable(const QUrl& uri) {
        return Unavailable(uri.toDisplayString());
    }

  private:
    FileSystemError(QString errorCode, const QString& message)
        : std::runtime_error((errorCode + ": " + message).toStdString()),
          code(std::move(errorCode)) {}
};
// Native names for upstream's anonymous option objects. delete_ is required
// because delete is a C++ keyword; other member names follow workspace.fs.
struct FileSystemDeleteOptions final {
    bool recursive{}, useTrash{};
};
struct FileSystemOverwriteOptions final {
    bool overwrite{};
};
class FileSystem {
  public:
    virtual ~FileSystem() = default;
    virtual QFuture<FileStat> stat(const QUrl& uri) = 0;
    virtual QFuture<QVector<std::pair<QString, FileType>>> readDirectory(const QUrl& uri) = 0;
    virtual QFuture<void> createDirectory(const QUrl& uri) = 0;
    virtual QFuture<QByteArray> readFile(const QUrl& uri) = 0;
    virtual QFuture<void> writeFile(const QUrl& uri, const QByteArray& content) = 0;
    virtual QFuture<void> delete_(const QUrl& uri, FileSystemDeleteOptions options = {}) = 0;
    virtual QFuture<void> rename(const QUrl& source, const QUrl& target,
                                 FileSystemOverwriteOptions options = {}) = 0;
    virtual QFuture<void> copy(const QUrl& source, const QUrl& target,
                               FileSystemOverwriteOptions options = {}) = 0;
    [[nodiscard]] virtual std::optional<bool> isWritableFileSystem(const QString& scheme) const = 0;
};
} // namespace litecode::extensions
