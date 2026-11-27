#include "extensions/DocumentWire.h"
#include <limits>

namespace litecode::extensions::wire {
void sendDocumentChange(const DocumentChange& change, const std::function<void(const QJsonObject&)>& send) {
    auto header = encodeDocumentChange(change);
    if (change.text.size() <= 32768) { send(header); return; }
    header.insert("command", "documentChangeBegin");
    header.remove("text");
    header.insert("length", QString::number(change.text.size()));
    send(header);
    quint64 sequence = 0;
    for (qsizetype offset = 0; offset < change.text.size();) {
        auto count = qMin<qsizetype>(32768, change.text.size() - offset);
        if (offset + count < change.text.size() && change.text.at(offset + count - 1).isHighSurrogate()) --count;
        send({{"command", "documentChangeChunk"}, {"id", change.id}, {"sequence", QString::number(sequence++)},
            {"text", change.text.mid(offset, count)}});
        offset += count;
    }
    send({{"command", "documentChangeEnd"}, {"id", change.id}, {"sequence", QString::number(sequence)}});
}
std::optional<DocumentChange> DocumentChangeDecoder::accept(const QJsonObject& message, QString* error) {
    if (error) error->clear();
    const auto fail = [&]() -> std::optional<DocumentChange> {
        pending_.reset();
        if (error) *error = "Invalid document edit transfer.";
        return {};
    };
    const auto command = message.value("command").toString();
    if (command == "documentChange") {
        if (pending_) return fail();
        const auto change = decodeDocumentChange(message);
        if (!change || change->text.size() > 32768) return fail();
        return change;
    }
    if (command == "documentChangeBegin") {
        if (pending_ || message.contains("text")) return fail();
        auto header = message;
        header.insert("text", QString{});
        auto change = decodeDocumentChange(header);
        bool valid = false;
        const auto lengthText = message.value("length").toString();
        const auto length = lengthText.toLongLong(&valid);
        if (!change || !valid || length <= 32768 || length > std::numeric_limits<int>::max() ||
            QString::number(length) != lengthText) return fail();
        pending_ = std::move(change);
        expected_ = length;
        sequence_ = 0;
        return {};
    }
    bool valid = false;
    const auto sequenceText = message.value("sequence").toString();
    const auto sequence = sequenceText.toULongLong(&valid);
    if (!pending_ || message.value("id").toString() != pending_->id || !valid ||
        sequence != sequence_ || QString::number(sequence) != sequenceText) return fail();
    if (command == "documentChangeChunk") {
        if (!message.value("text").isString()) return fail();
        const auto text = message.value("text").toString();
        if (text.isEmpty() || text.size() > 32768 || pending_->text.size() + text.size() > expected_) return fail();
        pending_->text += text;
        ++sequence_;
        return {};
    }
    if (command != "documentChangeEnd" || pending_->text.size() != expected_) return fail();
    return std::exchange(pending_, {});
}
} // namespace litecode::extensions::wire
