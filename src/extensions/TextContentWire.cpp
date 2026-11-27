#include "extensions/TextContentWire.h"
#include <stdexcept>

namespace litecode::extensions::wire {
namespace {
std::optional<quint64> number(const QJsonValue& value, bool zero = false) {
    if (!value.isString()) return {};
    bool valid = false;
    const auto result = value.toString().toULongLong(&valid);
    if (!valid || (!zero && !result) || QString::number(result) != value.toString()) return {};
    return result;
}
}
void sendTextContent(const TextContentTransfer& transfer, const std::function<void(const QJsonObject&)>& send) {
    if (!transfer.transferId || !transfer.handle || (!transfer.change && !transfer.requestId) ||
        (transfer.content && transfer.content->size() > TextContentDecoder::maximumLength()))
        throw std::invalid_argument("Invalid or oversized text content transfer.");
    send({{"event", "textContentBegin"}, {"transferId", QString::number(transfer.transferId)},
        {"handle", QString::number(transfer.handle)}, {"requestId", QString::number(transfer.requestId)},
        {"uri", transfer.uri.toString(QUrl::FullyEncoded)}, {"change", transfer.change},
        {"defined", transfer.content.has_value()}, {"length", QString::number(transfer.content ? transfer.content->size() : 0)},
        {"error", transfer.error}});
    quint64 sequence = 0;
    if (transfer.content) for (qsizetype offset = 0; offset < transfer.content->size();) {
        auto length = qMin<qsizetype>(32768, transfer.content->size() - offset);
        if (offset + length < transfer.content->size() && transfer.content->at(offset + length - 1).isHighSurrogate()) --length;
        send({{"event", "textContentChunk"}, {"transferId", QString::number(transfer.transferId)},
            {"sequence", QString::number(sequence++)}, {"text", transfer.content->mid(offset, length)}});
        offset += length;
    }
    send({{"event", "textContentEnd"}, {"transferId", QString::number(transfer.transferId)},
        {"sequence", QString::number(sequence)}});
}
std::optional<TextContentTransfer> TextContentDecoder::accept(const QJsonObject& message, QString* error) {
    if (error) error->clear();
    const auto fail = [&]() -> std::optional<TextContentTransfer> {
        reset();
        if (error) *error = QStringLiteral("Invalid text content transfer.");
        return {};
    };
    const auto id = number(message.value("transferId"));
    if (!id) return fail();
    const auto event = message.value("event").toString();
    if (event == "textContentBegin") {
        const auto handle = number(message.value("handle"));
        const auto request = number(message.value("requestId"), true);
        const auto length = number(message.value("length"), true);
        const QUrl uri(message.value("uri").toString());
        if (pending_.contains(*id) || !handle || !request || !length || *length > quint64(maximumLength()) ||
            qint64(*length) > maximumLength() - reserved_ || !message.value("uri").isString() || !uri.isValid() || uri.scheme().isEmpty() ||
            !message.value("change").isBool() || !message.value("defined").isBool() || !message.value("error").isString() ||
            (!message.value("change").toBool() && !*request) ||
            (!message.value("defined").toBool() && *length != 0) ||
            (message.value("change").toBool() && (!message.value("defined").toBool() || !message.value("error").toString().isEmpty()))) return fail();
        TextContentTransfer value{*id, *handle, *request, uri, {}, message.value("error").toString(), message.value("change").toBool()};
        if (message.value("defined").toBool()) { value.content = QString(); value.content->reserve(qsizetype(*length)); }
        pending_.insert(*id, Pending{std::move(value), qint64(*length), 0});
        reserved_ += qint64(*length);
        return {};
    }
    auto found = pending_.find(*id);
    const auto sequence = number(message.value("sequence"), true);
    if (found == pending_.end() || !sequence || *sequence != found->sequence) return fail();
    if (event == "textContentChunk") {
        if (!found->value.content || !message.value("text").isString()) return fail();
        const auto text = message.value("text").toString();
        if (text.isEmpty() || text.size() > 32768 || text.size() > found->length - found->value.content->size()) return fail();
        found->value.content->append(text);
        ++found->sequence;
        return {};
    }
    if (event != "textContentEnd" || (found->value.content && found->value.content->size() != found->length)) return fail();
    auto value = std::move(found->value);
    reserved_ -= found->length;
    pending_.erase(found);
    return value;
}
} // namespace litecode::extensions::wire
