#include "debug/DapProtocol.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace litecode::debug {
void DapDecoder::reset() {
    buffer_.clear();
    expected_ = -1;
    error_.clear();
}
QByteArray DapDecoder::encode(const QJsonObject& message) {
    const auto payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() > MaxPayloadBytes)
        return {};
    return "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload;
}
QVector<QJsonObject> DapDecoder::feed(const QByteArray& bytes) {
    QVector<QJsonObject> messages;
    if (!error_.isEmpty())
        return messages;
    qsizetype offset = 0;
    // Consume bounded chunks, including when several frames arrive in one read.
    while (offset < bytes.size()) {
        const auto count = std::min<qsizetype>(65536, bytes.size() - offset);
        buffer_.append(bytes.constData() + offset, count);
        offset += count;
        while (true) {
            if (expected_ < 0) {
                const auto boundary = buffer_.indexOf("\r\n\r\n");
                if (boundary < 0) {
                    if (buffer_.size() > MaxHeaderBytes)
                        error_ = "DAP header exceeds size limit.";
                    break;
                }
                if (boundary > MaxHeaderBytes) {
                    error_ = "DAP header exceeds size limit.";
                    break;
                }
                bool found = false;
                for (auto line : buffer_.left(boundary).split('\n')) {
                    if (line.endsWith('\r'))
                        line.chop(1);
                    const auto colon = line.indexOf(':');
                    if (colon <= 0) {
                        error_ = "Malformed DAP header.";
                        break;
                    }
                    if (line.left(colon).trimmed().toLower() != "content-length")
                        continue;
                    const auto value = line.mid(colon + 1).trimmed();
                    bool digits = !value.isEmpty();
                    for (const auto ch : value)
                        digits &= ch >= '0' && ch <= '9';
                    bool valid = false;
                    const auto length = value.toLongLong(&valid);
                    if (found || !digits || !valid || length <= 0 || length > MaxPayloadBytes) {
                        error_ = "Invalid or duplicate DAP Content-Length.";
                        break;
                    }
                    expected_ = static_cast<qsizetype>(length);
                    found = true;
                }
                if (!error_.isEmpty())
                    break;
                if (!found) {
                    error_ = "Missing DAP Content-Length.";
                    break;
                }
                buffer_.remove(0, boundary + 4);
            }
            if (buffer_.size() < expected_)
                break;
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(buffer_.left(expected_), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                error_ = "Invalid DAP JSON object.";
                break;
            }
            buffer_.remove(0, expected_);
            expected_ = -1;
            messages.append(document.object());
        }
        if (!error_.isEmpty()) {
            buffer_.clear();
            expected_ = -1;
            break;
        }
    }
    return messages;
}

static int positiveInteger(const QJsonValue& value) {
    if (!value.isDouble())
        return 0;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 1 || number > std::numeric_limits<int>::max() ||
        std::floor(number) != number)
        return 0;
    return static_cast<int>(number);
}
DapChannel::DapChannel(QObject* parent) : QObject(parent) {
    clock_.start();
    deadlines_.setInterval(100);
    connect(&deadlines_, &QTimer::timeout, this, &DapChannel::expire);
}
int DapChannel::nextSequence() {
    if (sequence_ == std::numeric_limits<int>::max()) {
        fail("DAP sequence exhausted.");
        return 0;
    }
    return ++sequence_;
}
int DapChannel::request(const QString& command, const QJsonObject& arguments, int timeoutMs) {
    return requestValue(command, arguments, timeoutMs);
}
int DapChannel::requestValue(const QString& command, const QJsonValue& arguments, int timeoutMs) {
    if (failed_ || resetting_ || command.isEmpty() || timeoutMs <= 0 || pending_.size() >= 1024)
        return 0;
    const int seq = nextSequence();
    if (!seq)
        return 0;
    QJsonObject message{{"seq", seq}, {"type", "request"}, {"command", command}};
    if (!arguments.isUndefined())
        message.insert("arguments", arguments);
    const auto frame = DapDecoder::encode(message);
    if (frame.isEmpty())
        return 0;
    pending_.insert(seq, {command, clock_.elapsed() + timeoutMs});
    deadlines_.start();
    const auto generation = generation_;
    emit messageSent(message);
    if (generation != generation_)
        return 0;
    emit writeRequested(frame, generation_);
    return seq;
}
bool DapChannel::respond(const QJsonObject& request, bool success, const QJsonObject& body,
                         const QString& message) {
    if (failed_ || resetting_ || request.value("type").toString() != "request" ||
        !positiveInteger(request.value("seq")) || request.value("command").toString().isEmpty())
        return false;
    const auto seq = nextSequence();
    if (!seq)
        return false;
    QJsonObject response{{"seq", seq},
                         {"type", "response"},
                         {"request_seq", request.value("seq")},
                         {"command", request.value("command")},
                         {"success", success},
                         {"body", body}};
    if (!message.isEmpty())
        response.insert("message", message);
    const auto frame = DapDecoder::encode(response);
    if (frame.isEmpty())
        return false;
    const auto generation = generation_;
    emit messageSent(response);
    if (generation != generation_)
        return false;
    emit writeRequested(frame, generation_);
    return true;
}
void DapChannel::receive(const QByteArray& bytes, quint64 generation) {
    if (generation != generation_ || failed_ || resetting_)
        return;
    const auto messages = decoder_.feed(bytes);
    if (!decoder_.error().isEmpty()) {
        fail(decoder_.error());
        return;
    }
    for (const auto& message : messages) {
        if (generation != generation_ || failed_)
            return;
        emit messageReceived(message);
        if (generation != generation_ || failed_)
            return;
        if (!positiveInteger(message.value("seq"))) {
            fail("Invalid DAP message sequence.");
            return;
        }
        const auto type = message.value("type").toString();
        if (type == "response") {
            const auto id = positiveInteger(message.value("request_seq"));
            const auto it = pending_.find(id);
            if (!id || !message.value("success").isBool() ||
                message.value("command").toString().isEmpty()) {
                fail("Malformed DAP response.");
                return;
            }
            if (it == pending_.end())
                continue; // Expired or already completed.
            if (it->command != message.value("command").toString()) {
                fail("DAP response command mismatch.");
                return;
            }
            const auto command = it->command;
            pending_.erase(it);
            if (pending_.isEmpty())
                deadlines_.stop();
            emit responseValueReceived(id, command, message.value("success").toBool(),
                                       message.value("body"), message.value("message").toString());
            if (generation != generation_)
                return;
            emit responseReceived(id, command, message.value("success").toBool(),
                                  message.value("body").toObject(),
                                  message.value("message").toString());
        } else if (type == "event") {
            const auto name = message.value("event").toString();
            if (name.isEmpty()) {
                fail("Missing DAP event name.");
                return;
            }
            emit eventValueReceived(name, message.value("body"));
            if (generation != generation_)
                return;
            emit eventReceived(name, message.value("body").toObject());
        } else if (type == "request") {
            if (message.value("command").toString().isEmpty()) {
                fail("Missing DAP reverse command.");
                return;
            }
            emit reverseRequestReceived(message);
        } else {
            fail("Unknown DAP message type.");
            return;
        }
    }
}
void DapChannel::expire() {
    const auto now = clock_.elapsed();
    const auto generation = generation_;
    const auto ids = pending_.keys();
    for (int id : ids) {
        if (generation != generation_)
            return;
        const auto it = pending_.find(id);
        if (it == pending_.end() || it->deadline > now)
            continue;
        const auto command = it->command;
        pending_.erase(it);
        emit responseReceived(id, command, false, {}, "Debug adapter request timed out.");
        emit responseValueReceived(id, command, false, QJsonValue::Undefined,
                                   "Debug adapter request timed out.");
    }
    if (pending_.isEmpty())
        deadlines_.stop();
}
void DapChannel::reset(const QString& reason) {
    if (resetting_)
        return;
    resetting_ = true;
    ++generation_;
    deadlines_.stop();
    const auto abandoned = std::exchange(pending_, {});
    decoder_.reset();
    sequence_ = 0;
    failed_ = false;
    for (auto it = abandoned.cbegin(); it != abandoned.cend(); ++it) {
        emit responseReceived(it.key(), it->command, false, {}, reason);
        emit responseValueReceived(it.key(), it->command, false, QJsonValue::Undefined, reason);
    }
    resetting_ = false;
}
void DapChannel::fail(const QString& message) {
    reset(message);
    failed_ = true;
    emit protocolError(message);
}
} // namespace litecode::debug
