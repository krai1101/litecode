#include "codex/TurnUndo.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace litecode::codex {
namespace {
struct Hunk {
    int oldStart{}, newStart{};
    QList<QByteArray> before, after;
};
struct Patch {
    QString oldPath, newPath;
    QVector<Hunk> hunks;
};
bool write(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QString hash(const QString& value) {
    return QString::fromLatin1(
        QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool safePath(const QString& root, const QString& path) {
    const QFileInfo info(path);
    const auto parent = info.dir().canonicalPath();
    const auto relative = QDir(root).relativeFilePath(parent);
    return !root.isEmpty() && !parent.isEmpty() && !info.isSymLink() && relative != ".." &&
           !relative.startsWith("../") && !QDir::isAbsolutePath(relative);
}
QJsonObject readRecord(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool writeRecord(const QString& path, const QJsonObject& record) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
    if (bytes.size() > 64 * 1024 * 1024)
        return false;
    if (!write(path, bytes))
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}
} // namespace
QString TurnUndo::recordPath(const QString& workspace, const QString& threadId,
                             const QString& turnId) {
    if (workspace.isEmpty() || threadId.isEmpty() || turnId.isEmpty())
        return {};
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath("codex-undo/" + hash(QFileInfo(workspace).canonicalFilePath() + '\n' + threadId) +
                  '/' + hash(turnId) + ".json");
}
bool TurnUndo::captureBefore(const QString& recordPath, const QString& workspace,
                             const QJsonArray& changes) {
    if (recordPath.isEmpty())
        return false;
    const auto root = QFileInfo(workspace).canonicalFilePath();
    auto record = readRecord(recordPath);
    if (QFileInfo::exists(recordPath) &&
        (record.value("workspace") != root || record.value("version").toInt() != 1))
        return false;
    if (record.value("complete").toBool())
        return true;
    auto baselines = record.value("baselines").toObject();
    for (const auto& value : changes) {
        const auto change = value.toObject();
        const auto path =
            QDir::cleanPath(QDir(workspace).absoluteFilePath(change.value("path").toString()));
        if (baselines.contains(path))
            continue;
        if (!safePath(root, path)) {
            record.insert("unsafe", true);
            continue;
        }
        const bool exists = QFileInfo::exists(path);
        QByteArray bytes;
        if (exists) {
            QFile file(path);
            if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) ||
                file.size() > 8 * 1024 * 1024) {
                record.insert("unsafe", true);
                continue;
            }
            bytes = file.readAll();
        }
        baselines.insert(path, QJsonObject{{"exists", exists},
                                           {"bytes", QString::fromLatin1(bytes.toBase64())}});
    }
    record.insert("version", 1);
    record.insert("workspace", root);
    record.insert("baselines", baselines);
    return writeRecord(recordPath, record);
}
QString TurnUndo::recordedDiff(const QString& recordPath, const QString& workspace) {
    if (recordPath.isEmpty())
        return {};
    const auto record = readRecord(recordPath);
    const auto diff = record.value("diff").toString();
    if (!record.value("complete").toBool() || record.value("version").toInt() != 1 ||
        record.value("workspace") != QFileInfo(workspace).canonicalFilePath() ||
        record.value("diffHash") != hash(diff))
        return {};
    return diff;
}
TurnUndo::TurnUndo(const QString& diff, const QString& workspace, const QString& recordPath)
    : recordPath_(recordPath), workspace_(QFileInfo(workspace).canonicalFilePath()),
      diffHash_(hash(diff)), diff_(diff) {
    auto reject = [this] {
        files_.clear();
        reason_ = QStringLiteral("Undo unavailable: the recorded patch cannot be safely reversed "
                                 "against the current files.");
    };
    const auto saved = recordPath_.isEmpty() ? QJsonObject{} : readRecord(recordPath_);
    if (!recordPath_.isEmpty() && QFileInfo::exists(recordPath_) &&
        (saved.value("version").toInt() != 1 || saved.value("workspace") != workspace_ ||
         saved.value("unsafe").toBool())) {
        reject();
        return;
    }
    if (saved.value("complete").toBool()) {
        const auto expected =
            hash(QString::fromUtf8(
                     QJsonDocument(saved.value("files").toArray()).toJson(QJsonDocument::Compact)) +
                 '\n' + workspace_ + '\n' + diffHash_ + '\n' +
                 QString::number(saved.value("used").toBool()));
        if (saved.value("diffHash") != diffHash_ || saved.value("snapshotHash") != expected) {
            reject();
            return;
        }
        for (const auto& value : saved.value("files").toArray()) {
            const auto data = value.toObject();
            File file;
            file.path = data.value("path").toString();
            file.canonicalParent = data.value("parent").toString();
            file.beforeExists = data.value("beforeExists").toBool();
            file.afterExists = data.value("afterExists").toBool();
            const auto before =
                QByteArray::fromBase64Encoding(data.value("before").toString().toLatin1(),
                                               QByteArray::AbortOnBase64DecodingErrors);
            const auto after = QByteArray::fromBase64Encoding(
                data.value("after").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            if (!before || !after || !safePath(workspace_, file.path) ||
                QFileInfo(file.path).dir().canonicalPath() != file.canonicalParent ||
                before.decoded.size() > 8 * 1024 * 1024 || after.decoded.size() > 8 * 1024 * 1024) {
                reject();
                return;
            }
            file.before = before.decoded;
            file.after = after.decoded;
            files_.append(file);
        }
        used_ = saved.value("used").toBool();
        if (used_)
            reason_ = QStringLiteral("This turn has already been undone.");
        if (files_.isEmpty())
            reject();
        return;
    }
    if (workspace.isEmpty() || diff.contains("\\ No newline") ||
        diff.contains("GIT binary patch") || diff.contains("old mode ") ||
        diff.contains("new mode ") || diff.contains("rename from ")) {
        reject();
        return;
    }
    QVector<Patch> patches;
    int oldRemaining = 0, newRemaining = 0;
    const QRegularExpression header(
        QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));
    auto path = [](QString value) {
        value = value.section('\t', 0, 0).trimmed();
        if (value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);
        if (value.startsWith("a/") || value.startsWith("b/"))
            value.remove(0, 2);
        return value;
    };
    auto normalizedDiff = diff;
    normalizedDiff.replace("\r\n", "\n");
    for (const auto& line : normalizedDiff.split('\n')) {
        if ((oldRemaining || newRemaining) && !patches.isEmpty() &&
            !patches.last().hunks.isEmpty()) {
            auto& hunk = patches.last().hunks.last();
            if (line.startsWith(' ')) {
                hunk.before.append(line.mid(1).toUtf8());
                hunk.after.append(line.mid(1).toUtf8());
                --oldRemaining;
                --newRemaining;
                continue;
            }
            if (line.startsWith('-')) {
                hunk.before.append(line.mid(1).toUtf8());
                --oldRemaining;
                continue;
            }
            if (line.startsWith('+')) {
                hunk.after.append(line.mid(1).toUtf8());
                --newRemaining;
                continue;
            }
            reject();
            return;
        }
        if (line.startsWith("diff --git "))
            patches.append(Patch{});
        else if (line.startsWith("--- ")) {
            if (patches.isEmpty() || !patches.last().newPath.isEmpty())
                patches.append(Patch{});
            patches.last().oldPath = path(line.mid(4));
        } else if (line.startsWith("+++ ") && !patches.isEmpty())
            patches.last().newPath = path(line.mid(4));
        else if (line.startsWith("@@")) {
            const auto match = header.match(line);
            if (!match.hasMatch() || patches.isEmpty()) {
                reject();
                return;
            }
            oldRemaining = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
            newRemaining = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();
            patches.last().hunks.append(
                {match.captured(1).toInt(), match.captured(3).toInt(), {}, {}});
        }
    }
    if (oldRemaining || newRemaining) {
        reject();
        return;
    }
    const auto root = QFileInfo(workspace).canonicalFilePath();
    for (const auto& patch : patches) {
        if (patch.hunks.isEmpty() || patch.oldPath.isEmpty() || patch.newPath.isEmpty() ||
            (patch.oldPath != "/dev/null" && patch.newPath != "/dev/null" &&
             patch.oldPath != patch.newPath)) {
            reject();
            return;
        }
        File file;
        file.beforeExists = patch.oldPath != "/dev/null";
        file.afterExists = patch.newPath != "/dev/null";
        file.path =
            QDir(workspace).absoluteFilePath(file.afterExists ? patch.newPath : patch.oldPath);
        const QFileInfo info(file.path);
        const auto parent = info.dir().canonicalPath();
        file.canonicalParent = parent;
        const auto relative = QDir(root).relativeFilePath(parent);
        if (root.isEmpty() || parent.isEmpty() || relative == ".." || relative.startsWith("../") ||
            QDir::isAbsolutePath(relative) || info.isSymLink() ||
            info.exists() != file.afterExists) {
            reject();
            return;
        }
        if (file.afterExists) {
            QFile current(file.path);
            if (!current.open(QIODevice::ReadOnly) || current.size() > 8 * 1024 * 1024) {
                reject();
                return;
            }
            file.after = current.readAll();
            if (file.after.contains('\0') || QString::fromUtf8(file.after).toUtf8() != file.after ||
                !file.after.endsWith('\n')) {
                reject();
                return;
            }
            const bool crlf = file.after.contains("\r\n");
            auto normalized = file.after;
            normalized.replace("\r\n", "\n");
            auto lines = normalized.split('\n');
            lines.removeLast();
            for (auto it = patch.hunks.crbegin(); it != patch.hunks.crend(); ++it) {
                const int start = qMax(0, it->newStart - 1);
                if (start + it->after.size() > lines.size()) {
                    reject();
                    return;
                }
                for (int i = 0; i < it->after.size(); ++i)
                    if (lines[start + i] != it->after[i]) {
                        reject();
                        return;
                    }
                for (int i = 0; i < it->after.size(); ++i)
                    lines.removeAt(start);
                for (int i = it->before.size() - 1; i >= 0; --i)
                    lines.insert(start, it->before[i]);
            }
            if (!file.beforeExists && !lines.isEmpty()) {
                reject();
                return;
            }
            file.before = lines.join(crlf ? "\r\n" : "\n");
            if (file.beforeExists)
                file.before += crlf ? "\r\n" : "\n";
        } else {
            if (patch.hunks.size() != 1 || patch.hunks.first().oldStart != 1 ||
                !patch.hunks.first().after.isEmpty()) {
                reject();
                return;
            }
            file.before = patch.hunks.first().before.join("\n") + '\n';
        }
        for (const auto& existing : files_)
            if (existing.path == file.path) {
                reject();
                return;
            }
        files_.append(file);
    }
    if (files_.isEmpty())
        reject();
    const auto baselines = saved.value("baselines").toObject();
    for (auto& file : files_) {
        if (!baselines.contains(file.path))
            continue;
        const auto baseline = baselines.value(file.path).toObject();
        const auto bytes = QByteArray::fromBase64Encoding(
            baseline.value("bytes").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!bytes || baseline.value("exists").toBool() != file.beforeExists ||
            bytes.decoded != file.before) {
            reject();
            return;
        }
        file.before = bytes.decoded;
    }
    if (available() && !recordPath_.isEmpty() && !save()) {
        reject();
        reason_ = QStringLiteral("Undo unavailable: the snapshot could not be saved.");
    }
}
bool TurnUndo::save() const {
    if (recordPath_.isEmpty())
        return true;
    QJsonArray files;
    for (const auto& file : files_)
        files.append(QJsonObject{{"path", file.path},
                                 {"parent", file.canonicalParent},
                                 {"beforeExists", file.beforeExists},
                                 {"afterExists", file.afterExists},
                                 {"before", QString::fromLatin1(file.before.toBase64())},
                                 {"after", QString::fromLatin1(file.after.toBase64())}});
    return writeRecord(
        recordPath_, {{"version", 1},
                      {"workspace", workspace_},
                      {"complete", true},
                      {"diffHash", diffHash_},
                      {"diff", diff_},
                      {"snapshotHash",
                       hash(QString::fromUtf8(QJsonDocument(files).toJson(QJsonDocument::Compact)) +
                            '\n' + workspace_ + '\n' + diffHash_ + '\n' + QString::number(used_))},
                      {"used", used_},
                      {"files", files}});
}
QVector<TurnUndo::Snapshot> TurnUndo::snapshots() const {
    QVector<Snapshot> result;
    for (const auto& file : files_)
        result.append({file.path, file.before, file.after});
    return result;
}
QStringList TurnUndo::paths() const {
    QStringList result;
    for (const auto& file : files_)
        result.append(file.path);
    return result;
}
bool TurnUndo::apply(QString* error) {
    if (!available()) {
        *error = reason_;
        return false;
    }
    for (const auto& file : files_) {
        QFile current(file.path);
        if (QFileInfo(file.path).isSymLink() ||
            QFileInfo(file.path).dir().canonicalPath() != file.canonicalParent ||
            current.exists() != file.afterExists ||
            (file.afterExists &&
             (!current.open(QIODevice::ReadOnly) || current.size() != file.after.size() ||
              current.readAll() != file.after))) {
            *error = QStringLiteral("Undo cancelled: a file changed since this turn was captured. "
                                    "No files were modified.");
            return false;
        }
    }
    int completed = 0;
    for (const auto& file : files_) {
        const bool success =
            file.beforeExists ? write(file.path, file.before) : QFile::moveToTrash(file.path);
        if (!success) {
            bool restored = true;
            for (int i = completed - 1; i >= 0; --i) {
                const auto& previous = files_[i];
                restored &= previous.afterExists ? write(previous.path, previous.after)
                                                 : QFile::remove(previous.path);
            }
            *error = restored ? QStringLiteral("Undo failed; earlier files were restored.")
                              : QStringLiteral("Undo failed and rollback was incomplete. Check the "
                                               "affected files and system trash.");
            return false;
        }
        ++completed;
    }
    used_ = true;
    if (!save()) {
        *error = QStringLiteral("Files were undone, but the snapshot status could not be saved.");
        return false;
    }
    return true;
}
} // namespace litecode::codex
