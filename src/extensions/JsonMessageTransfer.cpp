#include "extensions/JsonMessageTransfer.h"
#include "extensions/ExtensionWireProtocol.h"
#include <QJsonDocument>
#include <stdexcept>

namespace litecode::extensions::wire {
namespace {
std::optional<quint64> number(const QJsonValue& value, bool allowZero = false) {
    if (!value.isString()) return {};
    const auto text = value.toString();
    bool valid = false;
    const auto result = text.toULongLong(&valid);
    if (!valid || (!allowZero && !result) || QString::number(result) != text) return {};
    return result;
}
} // namespace
void sendJsonTransfer(const QJsonObject& message, const JsonTransferRoute& route,
    const std::function<void(const QJsonObject&)>& send) {
    if (route.field.isEmpty() || route.name.isEmpty() || route.prefix.isEmpty() ||
        message.value(route.field) != QJsonValue(route.name) || !number(message.value("requestId")))
        throw std::invalid_argument("Invalid JSON transfer identity.");
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxJsonTransferBytes) throw std::length_error("JSON transfer exceeds the host memory budget.");
    if (bytes.size() <= static_cast<qsizetype>(MaxMessageBytes) - 1024) { send(message); return; }
    const auto requestId = message.value("requestId");
    send({{route.field, route.prefix + "Begin"}, {"requestId", requestId}, {"length", QString::number(bytes.size())}});
    quint64 sequence = 0;
    for (qsizetype offset = 0; offset < bytes.size(); offset += JsonTransferChunkBytes) {
        send({{route.field, route.prefix + "Chunk"}, {"requestId", requestId}, {"sequence", QString::number(sequence++)},
            {"data", QString::fromLatin1(bytes.mid(offset, JsonTransferChunkBytes).toBase64())}});
    }
    send({{route.field, route.prefix + "End"}, {"requestId", requestId}, {"sequence", QString::number(sequence)}});
}
bool JsonMessageTransferDecoder::recognizes(const QJsonObject& message) const {
    const auto name = message.value(route_.field).toString();
    return name == route_.prefix + "Begin" || name == route_.prefix + "Chunk" || name == route_.prefix + "End";
}
void JsonMessageTransferDecoder::reset() {
    requestId_.clear();
    bytes_ = QByteArray{};
    length_ = 0;
    sequence_ = 0;
}
std::optional<QJsonObject> JsonMessageTransferDecoder::accept(const QJsonObject& message, QString* error) {
    if (error) error->clear();
    const auto fail = [&]() -> std::optional<QJsonObject> {
        reset();
        if (error) *error = QStringLiteral("Invalid %1 JSON transfer.").arg(route_.prefix);
        return {};
    };
    if (!number(message.value("requestId"))) return fail();
    const auto id = message.value("requestId").toString();
    const auto event = message.value(route_.field).toString();
    if (event == route_.prefix + "Begin") {
        const auto length = number(message.value("length"));
        if (transferring() || !length || *length > quint64(MaxJsonTransferBytes)) return fail();
        requestId_ = id;
        length_ = static_cast<qsizetype>(*length);
        return {};
    }
    const auto sequence = number(message.value("sequence"), true);
    if (!transferring() || id != requestId_ || !sequence || *sequence != sequence_) return fail();
    if (event == route_.prefix + "Chunk") {
        if (!message.value("data").isString()) return fail();
        const auto data = message.value("data").toString();
        if (data.isEmpty() || data.size() > ((JsonTransferChunkBytes + 2) / 3) * 4) return fail();
        const auto encoded = data.toLatin1();
        const auto decoded = QByteArray::fromBase64(encoded);
        if (decoded.isEmpty() || decoded.size() > JsonTransferChunkBytes ||
            decoded.size() > length_ - bytes_.size() || QString::fromLatin1(decoded.toBase64()) != data) return fail();
        bytes_.append(decoded);
        ++sequence_;
        return {};
    }
    if (event != route_.prefix + "End" || bytes_.size() != length_) return fail();
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes_, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return fail();
    auto reconstructed = document.object();
    if (reconstructed.value(route_.field) != QJsonValue(route_.name) ||
        reconstructed.value("requestId").toString() != requestId_ || !number(reconstructed.value("requestId"))) return fail();
    reset();
    return reconstructed;
}
} // namespace litecode::extensions::wire
