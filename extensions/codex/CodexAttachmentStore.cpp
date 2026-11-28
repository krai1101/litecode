#include "CodexAttachmentStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
namespace litecode::codex {
bool codexImageFile(const QString& path) {
    return QStringList{"png", "jpg", "jpeg", "webp", "gif", "bmp"}.contains(
        QFileInfo(path).suffix().toLower());
}
CodexAttachmentStore::CodexAttachmentStore(QString directory) : directory_(std::move(directory)) {}
QString CodexAttachmentStore::imagePath(const QString& source) {
    constexpr qsizetype limit = 24 * 1024 * 1024;
    if (source.isEmpty() || source.size() > limit * 4 / 3 + 256)
        return {};
    const auto url = QUrl(source);
    const auto local = url.isLocalFile() ? url.toLocalFile() : source;
    const QFileInfo info(local);
    const auto identity = source.startsWith("data:")
                              ? source
                              : source + QString::number(info.size()) +
                                    QString::number(info.lastModified().toMSecsSinceEpoch());
    const auto key = QString::fromLatin1(
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (const auto cached = resolved_.constFind(key);
        cached != resolved_.cend() && QFileInfo::exists(*cached))
        return *cached;
    QByteArray data;
    QString suffix;
    if (source.startsWith("data:image/")) {
        const auto comma = source.indexOf(',');
        if (comma < 0 || comma > 128 || !source.left(comma).endsWith(";base64"))
            return {};
        suffix = source.mid(11, comma - 11).section(';', 0, 0).toLower();
        if (!QStringList{"png", "jpeg", "jpg", "webp", "gif", "bmp"}.contains(suffix))
            return {};
        const auto decoded = QByteArray::fromBase64Encoding(
            source.mid(comma + 1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return {};
        data = decoded.decoded;
    } else {
        const auto& path = local;
        if (!codexImageFile(path))
            return {};
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
            return {};
        suffix = QFileInfo(path).suffix().toLower();
        data = file.readAll();
    }
    if (data.isEmpty() || data.size() > limit || !QDir().mkpath(directory_))
        return {};
    const auto digest =
        QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    const auto path = QDir(directory_).absoluteFilePath(digest + '.' + suffix);
    if (!QFileInfo::exists(path)) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            return {};
    }
    if (resolved_.size() >= 256)
        resolved_.clear();
    resolved_.insert(key, path);
    return path;
}
} // namespace litecode::codex
