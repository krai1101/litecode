#pragma once
#include "extensions/FileSystemApi.h"
#include <QThreadPool>

namespace litecode::extensions {
// Private file:// backend. Work runs outside the host event loop; native
// extensions retain the same user-level filesystem access as their process.
class LocalFileSystem final : public FileSystem {
  public:
    LocalFileSystem();
    QFuture<FileStat> stat(const QUrl& uri) override;
    QFuture<QVector<std::pair<QString, FileType>>> readDirectory(const QUrl& uri) override;
    QFuture<void> createDirectory(const QUrl& uri) override;
    QFuture<QByteArray> readFile(const QUrl& uri) override;
    QFuture<void> writeFile(const QUrl& uri, const QByteArray& content) override;
    QFuture<void> delete_(const QUrl& uri, FileSystemDeleteOptions options = {}) override;
    QFuture<void> rename(const QUrl& source, const QUrl& target, FileSystemOverwriteOptions options = {}) override;
    QFuture<void> copy(const QUrl& source, const QUrl& target, FileSystemOverwriteOptions options = {}) override;
    std::optional<bool> isWritableFileSystem(const QString& scheme) const override;
  private:
    QThreadPool workers_;
};
} // namespace litecode::extensions
