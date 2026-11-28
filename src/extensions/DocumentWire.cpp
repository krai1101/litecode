#include "extensions/DocumentWire.h"
#include <limits>

namespace litecode::extensions::wire {
void sendDocumentBatch(const DocumentBatch& batch,
                       const std::function<void(const QJsonObject&)>& send) {
    auto header = encodeDocumentChange({batch.id,
                                        batch.previousVersion,
                                        batch.version,
                                        Range(0, 0, 0, 0),
                                        {},
                                        batch.dirty,
                                        batch.reason});
    header.insert("command", "documentBatchBegin");
    header.insert("count", QString::number(batch.edits.size()));
    send(header);
    for (const auto& edit : batch.edits)
        sendDocumentChange({batch.id, batch.previousVersion, batch.version, edit.range,
                            edit.newText, batch.dirty, batch.reason},
                           send);
    send({{"command", "documentBatchEnd"}, {"id", batch.id}});
}
std::optional<DocumentBatch> DocumentBatchDecoder::accept(const QJsonObject& message,
                                                          QString* error) {
    if (error)
        error->clear();
    const auto fail = [this, error]() -> std::optional<DocumentBatch> {
        pending_.reset();
        changes_ = {};
        expected_ = length_ = 0;
        if (error)
            *error = QStringLiteral("Invalid document batch transfer.");
        return {};
    };
    const auto command = message.value("command").toString();
    if (command == "documentBatchBegin") {
        if (pending_)
            return fail();
        const auto header = decodeDocumentChange(message);
        const auto countText = message.value("count").toString();
        bool valid = false;
        const auto count = countText.toLongLong(&valid);
        if (!header || !header->text.isEmpty() || !valid || count < 1 || count > 100000 ||
            QString::number(count) != countText)
            return fail();
        pending_ = DocumentBatch{header->id, header->previousVersion, header->version,
                                 {},         header->dirty,           header->reason};
        expected_ = count;
        length_ = 0;
        return {};
    }
    if (!pending_ || message.value("id").toString() != pending_->id)
        return fail();
    if (command == "documentBatchEnd") {
        if (changes_.transferring() || pending_->edits.size() != expected_)
            return fail();
        return std::exchange(pending_, {});
    }
    if (pending_->edits.size() >= expected_)
        return fail();
    if (command == "documentChangeBegin") {
        bool valid = false;
        const auto length = message.value("length").toString().toLongLong(&valid);
        // Bound the in-progress chunk buffer, not only completed operations.
        if (!valid || length > 50 * 1024 * 1024 - length_)
            return fail();
    }
    QString diagnostic;
    const auto change = changes_.accept(message, &diagnostic);
    if (!diagnostic.isEmpty())
        return fail();
    if (change) {
        if (change->previousVersion != pending_->previousVersion ||
            change->version != pending_->version || change->dirty != pending_->dirty ||
            change->reason != pending_->reason)
            return fail();
        length_ += change->text.size();
        if (length_ > 50 * 1024 * 1024)
            return fail();
        pending_->edits.append(TextEdit(change->range, change->text));
    }
    return {};
}
QJsonObject encodeDocumentMetadata(const TextDocumentMetadata& metadata) {
    return {{"command", "documentMetadata"},
            {"id", metadata.id},
            {"languageId", metadata.languageId},
            {"encoding", metadata.encoding},
            {"dirty", metadata.dirty}};
}
std::optional<TextDocumentMetadata> decodeDocumentMetadata(const QJsonObject& message) {
    if (message.value("id").toString().isEmpty() || !message.value("languageId").isString() ||
        !message.value("encoding").isString() || !message.value("dirty").isBool())
        return {};
    return TextDocumentMetadata{
        message.value("id").toString(), message.value("languageId").toString(),
        message.value("encoding").toString(), message.value("dirty").toBool()};
}
QJsonObject encodeDocumentChange(const DocumentChange& change) {
    const auto position = [](const Position& point) {
        return QJsonObject{{"line", point.line()}, {"character", point.character()}};
    };
    QJsonObject result{{"command", "documentChange"},
                       {"id", change.id},
                       {"previousVersion", QString::number(change.previousVersion)},
                       {"version", QString::number(change.version)},
                       {"range", QJsonObject{{"start", position(change.range.start())},
                                             {"end", position(change.range.end())}}},
                       {"text", change.text},
                       {"dirty", change.dirty}};
    if (change.reason)
        result.insert("reason", static_cast<int>(*change.reason));
    return result;
}
std::optional<DocumentChange> decodeDocumentChange(const QJsonObject& message) {
    const auto decodePosition = [](const QJsonValue& value) -> std::optional<Position> {
        if (!value.isObject())
            return {};
        const auto point = value.toObject();
        const auto line = point.value("line"), character = point.value("character");
        if (!line.isDouble() || !character.isDouble() || line.toInt(-1) < 0 ||
            character.toInt(-1) < 0 || line.toDouble() != line.toInt(-1) ||
            character.toDouble() != character.toInt(-1))
            return {};
        return Position(line.toInt(), character.toInt());
    };
    bool oldValid = false, newValid = false;
    const auto oldText = message.value("previousVersion").toString(),
               newText = message.value("version").toString();
    const auto oldVersion = oldText.toLongLong(&oldValid),
               newVersion = newText.toLongLong(&newValid);
    const auto encodedRange = message.value("range").toObject();
    const auto start = decodePosition(encodedRange.value("start")),
               end = decodePosition(encodedRange.value("end"));
    if (message.value("id").toString().isEmpty() || !oldValid || !newValid || oldVersion < 1 ||
        newVersion <= oldVersion || QString::number(oldVersion) != oldText ||
        QString::number(newVersion) != newText || !start || !end || start->isAfter(*end) ||
        !message.value("text").isString() || !message.value("dirty").isBool())
        return {};
    std::optional<TextDocumentChangeReason> reason;
    if (message.contains("reason")) {
        const auto value = message.value("reason");
        if (!value.isDouble() || (value.toDouble() != 1 && value.toDouble() != 2))
            return {};
        reason = static_cast<TextDocumentChangeReason>(value.toInt());
    }
    return DocumentChange{message.value("id").toString(),
                          oldVersion,
                          newVersion,
                          Range(*start, *end),
                          message.value("text").toString(),
                          message.value("dirty").toBool(),
                          reason};
}
void sendDocumentSnapshot(const TextDocumentSnapshot& snapshot,
                          const std::function<void(const QJsonObject&)>& send) {
    QJsonObject header{{"command", "documentBegin"},
                       {"id", snapshot.id},
                       {"uri", snapshot.uri.toString(QUrl::FullyEncoded)},
                       {"languageId", snapshot.languageId},
                       {"encoding", snapshot.encoding},
                       {"version", QString::number(snapshot.version)},
                       {"dirty", snapshot.dirty},
                       {"eol", static_cast<int>(snapshot.eol)},
                       {"length", QString::number(snapshot.text.size())}};
    if (snapshot.reason)
        header.insert("reason", static_cast<int>(*snapshot.reason));
    send(header);
    quint64 sequence = 0;
    for (qsizetype offset = 0; offset < snapshot.text.size();) {
        auto count = qMin<qsizetype>(32768, snapshot.text.size() - offset);
        if (offset + count < snapshot.text.size() &&
            snapshot.text.at(offset + count - 1).isHighSurrogate())
            --count;
        send({{"command", "documentChunk"},
              {"id", snapshot.id},
              {"sequence", QString::number(sequence++)},
              {"text", snapshot.text.mid(offset, count)}});
        offset += count;
    }
    send(
        {{"command", "documentEnd"}, {"id", snapshot.id}, {"sequence", QString::number(sequence)}});
}
std::optional<TextDocumentSnapshot> DocumentSnapshotDecoder::accept(const QJsonObject& message,
                                                                    QString* error) {
    const auto fail = [&]() -> std::optional<TextDocumentSnapshot> {
        pending_.reset();
        if (error)
            *error = "Invalid document snapshot transfer.";
        return {};
    };
    if (error)
        error->clear();
    const auto command = message.value("command").toString();
    const auto id = message.value("id").toString();
    if (id.isEmpty())
        return fail();
    if (command == "documentBegin") {
        bool versionValid = false, lengthValid = false;
        const auto versionText = message.value("version").toString(),
                   lengthText = message.value("length").toString();
        const auto version = versionText.toLongLong(&versionValid),
                   length = lengthText.toLongLong(&lengthValid);
        const QUrl uri(message.value("uri").toString());
        const auto eol = message.value("eol").toInt();
        if (pending_ || !versionValid || version < 1 || QString::number(version) != versionText ||
            !lengthValid || length < 0 || length > std::numeric_limits<int>::max() ||
            QString::number(length) != lengthText || uri.isEmpty() || !uri.isValid() ||
            uri.scheme().isEmpty() || !message.value("dirty").isBool() ||
            !message.value("languageId").isString() || !message.value("encoding").isString() ||
            (eol != 1 && eol != 2))
            return fail();
        pending_ = TextDocumentSnapshot{id,
                                        uri,
                                        message.value("languageId").toString(),
                                        message.value("encoding").toString(),
                                        {},
                                        version,
                                        message.value("dirty").toBool(),
                                        static_cast<EndOfLine>(eol)};
        if (message.contains("reason")) {
            const auto reason = message.value("reason");
            if (!reason.isDouble() || (reason.toDouble() != 1 && reason.toDouble() != 2))
                return fail();
            pending_->reason = static_cast<TextDocumentChangeReason>(reason.toInt());
        }
        expected_ = length;
        sequence_ = 0;
        return {};
    }
    bool valid = false;
    const auto sequenceText = message.value("sequence").toString();
    const auto sequence = sequenceText.toULongLong(&valid);
    if (!pending_ || pending_->id != id || !valid || sequence != sequence_ ||
        QString::number(sequence) != sequenceText)
        return fail();
    if (command == "documentChunk") {
        if (!message.value("text").isString())
            return fail();
        const auto text = message.value("text").toString();
        if (text.isEmpty() || text.size() > 32768 ||
            pending_->text.size() + text.size() > expected_)
            return fail();
        pending_->text += text;
        ++sequence_;
        return {};
    }
    if (command != "documentEnd" || pending_->text.size() != expected_)
        return fail();
    return std::exchange(pending_, {});
}
} // namespace litecode::extensions::wire
