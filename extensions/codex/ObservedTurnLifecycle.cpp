#include "ObservedTurnLifecycle.h"
#include "ObservedToolActivity.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace litecode::codex {
namespace {
constexpr int ChunkSize = 64 * 1024;
bool readEvent(const QByteArray& line, ObservedTurnLifecycle& result) {
    // Skip image/message payloads before JSON decoding. Lifecycle records are
    // small, even when adjacent transcript records contain megabytes of images.
    if (!line.contains("\"event_msg\"") || !line.contains(result.turnId.toUtf8()))
        return false;
    const auto record = QJsonDocument::fromJson(line).object();
    if (record.value("type") != "event_msg")
        return false;
    const auto payload = record.value("payload").toObject();
    if (payload.value("turn_id") != result.turnId)
        return false;
    const auto type = payload.value("type").toString();
    if (type != "task_started" && type != "task_complete" && type != "turn_aborted")
        return false;
    const auto timestamp =
        QDateTime::fromString(record.value("timestamp").toString(), Qt::ISODateWithMs);
    const auto time = timestamp.isValid() ? timestamp.toMSecsSinceEpoch() : 0;
    result.startedMs = payload.value("started_at").toInteger() * 1000;
    if (type == "task_started") {
        result.status = "inProgress";
        if (!result.startedMs)
            result.startedMs = time;
    } else {
        result.status = type == "turn_aborted"              ? "interrupted"
                        : payload.value("error").isObject() ? "failed"
                                                            : "completed";
        result.completedMs = payload.value("completed_at").toInteger() * 1000;
        if (!result.completedMs)
            result.completedMs = time;
    }
    return true;
}
} // namespace
ObservedTurnLifecycle readObservedTurnLifecycle(const QString& path, const QString& turnId,
                                              const ObservedTurnLifecycle& cached,
                                              const std::atomic_bool* cancelled) {
    ObservedTurnLifecycle result;
    if (path.isEmpty() || turnId.isEmpty())
        return result;
    const QFileInfo info(path);
    if (cached.path == path && cached.turnId == turnId && cached.fileSize == info.size() &&
        cached.modifiedMs == info.lastModified().toMSecsSinceEpoch())
        return cached;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    result.path = path;
    result.turnId = turnId;
    result.fileSize = file.size();
    result.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    ObservedToolActivity tools;
    const auto readRecord = [&](const QByteArray& line) {
        if (line.contains("\"response_item\"") && line.contains("\"call_id\""))
            tools.read(QJsonDocument::fromJson(line).object());
        if (!readEvent(line, result))
            return false;
        if (result.status == "inProgress")
            result.activeTools = tools.pending();
        return true;
    };
    qint64 position = result.fileSize;
    QByteArray suffix;
    bool discard = true; // Ignore an unterminated record still being written.
    bool oversized = false;
    while (position > 0) {
        if (cancelled && cancelled->load())
            return {};
        const auto count = qMin<qint64>(position, ChunkSize);
        position -= count;
        if (!file.seek(position))
            return {};
        const auto block = file.read(count);
        if (block.size() != count)
            return {};
        int end = int(block.size());
        int newline = int(block.lastIndexOf('\n'));
        while (newline >= 0) {
            const auto length = end - newline - 1;
            if (oversized || (!discard && length + suffix.size() > ChunkSize)) {
                // Read from the record's start so envelope fields split across
                // chunk boundaries are still available, without reading its data.
                if (!file.seek(position + newline + 1))
                    return {};
                tools.readOutputPrefix(file.read(4096));
            }
            if (!discard && length + suffix.size() <= ChunkSize &&
                readRecord(block.mid(newline + 1, length) + suffix))
                return result;
            suffix.clear();
            discard = false;
            oversized = false;
            end = newline;
            newline = newline > 0 ? int(block.lastIndexOf('\n', newline - 1)) : -1;
        }
        if (!discard && end + suffix.size() <= ChunkSize)
            suffix.prepend(block.left(end));
        else {
            oversized |= !discard;
            discard = true;
            suffix.clear();
        }
    }
    if (!discard)
        readRecord(suffix);
    return result;
}
QJsonObject reconcileObservedTurn(QJsonObject turn, const ObservedTurnLifecycle& lifecycle) {
    if (turn.value("id") != lifecycle.turnId || lifecycle.status.isEmpty() ||
        turn.value("status") != "interrupted" || turn.value("completedAt").isDouble() ||
        turn.value("durationMs").isDouble())
        return turn;
    turn.insert("status", lifecycle.status);
    if (!lifecycle.activeTools.isEmpty()) {
        auto items = turn.value("items").toArray();
        QSet<QString> existing;
        for (const auto& value : items)
            existing.insert(value.toObject().value("id").toString());
        for (const auto& value : lifecycle.activeTools)
            if (!existing.contains(value.toObject().value("id").toString()))
                items.append(value);
        turn.insert("items", items);
    }
    if (!turn.value("startedAt").isDouble() && lifecycle.startedMs > 0)
        turn.insert("startedAt", lifecycle.startedMs / 1000);
    if (lifecycle.completedMs > 0) {
        turn.insert("completedAt", lifecycle.completedMs / 1000);
        const auto started = turn.value("startedAt").toInteger() * 1000;
        if (started > 0)
            turn.insert("durationMs", qMax<qint64>(0, lifecycle.completedMs - started));
    }
    return turn;
}
} // namespace litecode::codex
