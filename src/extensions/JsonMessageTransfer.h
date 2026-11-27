#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <optional>
#include <utility>

namespace litecode::extensions::wire {
// Routes are fixed by the receiver, never selected by the peer. Reconstruction
// cannot turn a clipboard message into another RPC. One transfer per decoder.
struct JsonTransferRoute final { QString field, name, prefix; };
inline constexpr qsizetype JsonTransferChunkBytes = 32768;
inline constexpr qsizetype MaxJsonTransferBytes = 256 * 1024 * 1024;
void sendJsonTransfer(const QJsonObject& message, const JsonTransferRoute& route,
    const std::function<void(const QJsonObject&)>& send);
class JsonMessageTransferDecoder final {
  public:
    explicit JsonMessageTransferDecoder(JsonTransferRoute route) : route_(std::move(route)) {}
    std::optional<QJsonObject> accept(const QJsonObject& message, QString* error);
    [[nodiscard]] bool transferring() const { return !requestId_.isEmpty(); }
    [[nodiscard]] bool recognizes(const QJsonObject& message) const;
    void reset();
  private:
    JsonTransferRoute route_;
    QString requestId_;
    QByteArray bytes_;
    qsizetype length_{};
    quint64 sequence_{};
};
} // namespace litecode::extensions::wire
