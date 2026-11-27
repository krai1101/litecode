#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace litecode::extensions::wire {

inline constexpr int ProtocolVersion = 1;
inline constexpr quint32 MaxMessageBytes = 8 * 1024 * 1024;

// The private editor/host transport is independent of the public plugin SDK.
// Four big-endian bytes give the length of a UTF-8 JSON envelope. A zero or
// oversized length, unsupported version, or malformed envelope is fatal to
// this connection; callers must discard the decoder and terminate the host.
[[nodiscard]] QByteArray encode(const QJsonObject& message);

struct DecodeResult {
    QVector<QJsonObject> messages;
    QString error;
};

class Decoder final {
  public:
    [[nodiscard]] DecodeResult consume(const QByteArray& bytes);
    void reset();

  private:
    QByteArray header_;
    QByteArray body_;
    quint32 expectedSize_{};
    bool failed_{};
};

} // namespace litecode::extensions::wire
