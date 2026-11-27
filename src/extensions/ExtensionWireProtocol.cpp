#include "extensions/ExtensionWireProtocol.h"

#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>

namespace litecode::extensions::wire {

QByteArray encode(const QJsonObject& message) {
    const QByteArray body =
        QJsonDocument(QJsonObject{{"protocol", ProtocolVersion}, {"message", message}})
            .toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > MaxMessageBytes)
        return {};

    QByteArray frame;
    frame.resize(4);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame += body;
    return frame;
}

DecodeResult Decoder::consume(const QByteArray& bytes) {
    DecodeResult result;
    if (failed_) {
        result.error = "Extension host protocol is already invalid.";
        return result;
    }
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        if (expectedSize_ == 0) {
            const qsizetype count = std::min<qsizetype>(4 - header_.size(), bytes.size() - offset);
            header_.append(bytes.constData() + offset, count);
            offset += count;
            if (header_.size() != 4)
                break;
            expectedSize_ = qFromBigEndian<quint32>(header_.constData());
            header_.clear();
            if (expectedSize_ == 0 || expectedSize_ > MaxMessageBytes) {
                result.error = "Invalid extension host message length.";
                break;
            }
            body_.reserve(expectedSize_);
        }
        const qsizetype count =
            std::min<qsizetype>(expectedSize_ - body_.size(), bytes.size() - offset);
        body_.append(bytes.constData() + offset, count);
        offset += count;
        if (body_.size() != expectedSize_)
            break;

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(body_, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
            document.object().value("protocol").toInt(-1) != ProtocolVersion ||
            !document.object().value("message").isObject()) {
            result.error = "Invalid or unsupported extension host message.";
            break;
        }
        result.messages.append(document.object().value("message").toObject());
        body_.clear();
        expectedSize_ = 0;
    }
    if (!result.error.isEmpty()) {
        failed_ = true;
        header_.clear();
        body_.clear();
        expectedSize_ = 0;
        result.messages.clear();
    }
    return result;
}

void Decoder::reset() {
    header_.clear();
    body_.clear();
    expectedSize_ = 0;
    failed_ = false;
}

} // namespace litecode::extensions::wire
