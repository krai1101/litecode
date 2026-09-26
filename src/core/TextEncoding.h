#pragma once

#include "core/DocumentTypes.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace litecode::core {

[[nodiscard]] std::optional<TextEncoding> textEncodingFromByteOrderMark(const QByteArray& bytes);
[[nodiscard]] std::optional<TextEncoding> guessTextEncoding(QByteArrayView bytes);
[[nodiscard]] bool decodeText(const QByteArray& bytes, TextEncoding fallbackEncoding,
                              bool honorByteOrderMark, QByteArray* utf8,
                              TextEncoding* actualEncoding, QString* errorMessage = nullptr,
                              bool* hadByteOrderMark = nullptr, bool guessEncoding = false,
                              bool* hadDecodingErrors = nullptr);
[[nodiscard]] bool encodeText(const QByteArray& utf8, TextEncoding encoding, QByteArray* bytes,
                              QString* errorMessage = nullptr,
                              std::optional<bool> includeByteOrderMark = std::nullopt);
[[nodiscard]] QString ripgrepEncodingName(TextEncoding encoding);

} // namespace litecode::core
