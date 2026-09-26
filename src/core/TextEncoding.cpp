#include "core/TextEncoding.h"

#include "core/LegacySingleByteCodecData.h"

#include <QStringConverter>
#include <QTextCodec>

#include <uchardet.h>

#include <algorithm>

namespace litecode::core {
namespace {

using SingleByteTable = std::array<char16_t, 128>;

// Qt's legacy codec set does not include ISO-8859-11 on every platform.
// Keep this small, fixed mapping available for both decoding and lossless saves.
constexpr SingleByteTable iso885911 = [] {
    SingleByteTable table{};
    table.fill(u'\uFFFD');
    for (char16_t byte = 0x80; byte <= 0x9f; ++byte)
        table[byte - 0x80] = byte;
    table[0xa0 - 0x80] = 0x00a0;
    for (char16_t byte = 0xa1; byte <= 0xda; ++byte)
        table[byte - 0x80] = byte - 0xa1 + 0x0e01;
    table[0xdf - 0x80] = 0x0e3f;
    for (char16_t byte = 0xe0; byte <= 0xfb; ++byte)
        table[byte - 0x80] = byte - 0xe0 + 0x0e40;
    return table;
}();

// KOI8-RU is not among Qt's QTextCodec names. This table follows its published
// byte mapping, including the punctuation and Ukrainian/Belarusian extensions.
constexpr SingleByteTable koi8ru{
    0x2500, 0x2502, 0x250c, 0x2510, 0x2514, 0x2518, 0x251c, 0x2524, 0x252c, 0x2534, 0x253c, 0x2580,
    0x2584, 0x2588, 0x258c, 0x2590, 0x2591, 0x2592, 0x2593, 0x201c, 0x25a0, 0x2219, 0x201d, 0x2014,
    0x2116, 0x2122, 0x00a0, 0x00bb, 0x00ae, 0x00ab, 0x00b7, 0x00a4, 0x2550, 0x2551, 0x2552, 0x0451,
    0x0454, 0x2554, 0x0456, 0x0457, 0x2557, 0x2558, 0x2559, 0x255a, 0x255b, 0x0491, 0x045e, 0x255e,
    0x255f, 0x2560, 0x2561, 0x0401, 0x0404, 0x2563, 0x0406, 0x0407, 0x2566, 0x2567, 0x2568, 0x2569,
    0x256a, 0x0490, 0x040e, 0x00a9, 0x044e, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433,
    0x0445, 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e, 0x043f, 0x044f, 0x0440, 0x0441,
    0x0442, 0x0443, 0x0436, 0x0432, 0x044c, 0x044b, 0x0437, 0x0448, 0x044d, 0x0449, 0x0447, 0x044a,
    0x042e, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413, 0x0425, 0x0418, 0x0419, 0x041a,
    0x041b, 0x041c, 0x041d, 0x041e, 0x041f, 0x042f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412,
    0x042c, 0x042b, 0x0417, 0x0428, 0x042d, 0x0429, 0x0427, 0x042a,
};

const SingleByteTable* singleByteTable(TextEncoding encoding) {
    using namespace detail;
    switch (encoding) {
    case TextEncoding::Cp437:
        return &cp437;
    case TextEncoding::Cp852:
        return &cp852;
    case TextEncoding::Cp1125:
        return &cp1125;
    case TextEncoding::Cp857:
        return &cp857;
    case TextEncoding::Koi8T:
        return &koi8t;
    case TextEncoding::Cp865:
        return &cp865;
    case TextEncoding::Iso885911:
        return &iso885911;
    case TextEncoding::Koi8Ru:
        return &koi8ru;
    default:
        return nullptr;
    }
}

bool isValidUtf8(const QByteArray& text) {
    QStringDecoder decoder(QStringConverter::Utf8);
    const QString decoded = decoder.decode(text);
    Q_UNUSED(decoded);
    return !decoder.hasError();
}

QString normalizedEncodingName(QByteArrayView name) {
    QString result;
    for (const QChar character : QString::fromLatin1(name)) {
        if (character.isLetterOrNumber())
            result.append(character.toLower());
    }
    return result;
}

QByteArray byteOrderMark(TextEncoding encoding) {
    switch (encoding) {
    case TextEncoding::Utf8:
    case TextEncoding::Utf8Bom:
        return QByteArrayLiteral("\xEF\xBB\xBF");
    case TextEncoding::Utf16Le:
        return QByteArrayLiteral("\xFF\xFE");
    case TextEncoding::Utf16Be:
        return QByteArrayLiteral("\xFE\xFF");
    default:
        return {};
    }
}

struct ZeroByteDetection final {
    std::optional<TextEncoding> encoding;
    bool seemsBinary{};
};

ZeroByteDetection detectUtf16OrBinary(const QByteArray& bytes) {
    const qsizetype sampleSize = qMin<qsizetype>(bytes.size(), 512);
    const qsizetype pairCount = sampleSize / 2;
    qsizetype evenZeros = 0;
    qsizetype oddZeros = 0;
    for (qsizetype index = 0; index + 1 < sampleSize; index += 2) {
        evenZeros += bytes.at(index) == '\0';
        oddZeros += bytes.at(index + 1) == '\0';
    }
    if (evenZeros == 0 && oddZeros == 0)
        return {};
    // BOM-less UTF-16 text containing ASCII has a strong zero-byte bias on one side of each
    // code unit. Requiring several biased pairs avoids treating an isolated NUL in binary data
    // as text while still allowing non-ASCII code units among the sample.
    if (oddZeros >= 2 && oddZeros * 3 >= pairCount && oddZeros >= evenZeros * 4)
        return {TextEncoding::Utf16Le, false};
    if (evenZeros >= 2 && evenZeros * 3 >= pairCount && evenZeros >= oddZeros * 4)
        return {TextEncoding::Utf16Be, false};
    return {std::nullopt, true};
}

QString decodeUtf16(QByteArrayView bytes, bool littleEndian, bool* hadErrors) {
    QString result;
    result.reserve((bytes.size() + 1) / 2);
    for (qsizetype index = 0; index + 1 < bytes.size();) {
        const auto first = static_cast<quint8>(bytes.at(index));
        const auto second = static_cast<quint8>(bytes.at(index + 1));
        const quint16 unit =
            littleEndian ? quint16(first | (second << 8)) : quint16((first << 8) | second);
        index += 2;
        if (QChar::isHighSurrogate(unit)) {
            if (index + 1 < bytes.size()) {
                const auto nextFirst = static_cast<quint8>(bytes.at(index));
                const auto nextSecond = static_cast<quint8>(bytes.at(index + 1));
                const quint16 next = littleEndian ? quint16(nextFirst | (nextSecond << 8))
                                                  : quint16((nextFirst << 8) | nextSecond);
                if (QChar::isLowSurrogate(next)) {
                    result.append(QChar(unit));
                    result.append(QChar(next));
                    index += 2;
                    continue;
                }
            }
            *hadErrors = true;
            result.append(QChar::ReplacementCharacter);
        } else if (QChar::isLowSurrogate(unit)) {
            *hadErrors = true;
            result.append(QChar::ReplacementCharacter);
        } else {
            result.append(QChar(unit));
        }
    }
    if (bytes.size() % 2 != 0) {
        *hadErrors = true;
        result.append(QChar::ReplacementCharacter);
    }
    return result;
}

QString decodeSingleByte(QByteArrayView bytes, const SingleByteTable& table, bool* hadErrors) {
    QString result;
    result.reserve(bytes.size());
    for (const char value : bytes) {
        const auto byte = static_cast<quint8>(value);
        const QChar decoded = byte < 0x80 ? QChar(byte) : QChar(table.at(byte - 0x80));
        *hadErrors = *hadErrors || decoded == QChar::ReplacementCharacter;
        result.append(decoded);
    }
    return result;
}

QByteArray encodeSingleByte(QStringView text, const SingleByteTable& table, bool* lossless) {
    QByteArray result;
    result.reserve(text.size());
    *lossless = true;
    for (const QChar character : text) {
        if (character.unicode() < 0x80) {
            result.append(static_cast<char>(character.unicode()));
            continue;
        }
        const auto match = std::find(table.cbegin(), table.cend(), character.unicode());
        if (match != table.cend() && character != QChar::ReplacementCharacter) {
            result.append(static_cast<char>(std::distance(table.cbegin(), match) + 0x80));
        } else {
            *lossless = false;
            result.append('?');
        }
    }
    return result;
}

QString decodeWithCodec(QByteArrayView bytes, QTextCodec& codec, bool* hadErrors) {
    QTextCodec::ConverterState state;
    const QString result = codec.toUnicode(bytes.data(), bytes.size(), &state);
    *hadErrors = *hadErrors || state.invalidChars > 0;
    return result;
}

QString decodeGb18030(QByteArrayView bytes, QTextCodec& codec, bool* hadErrors) {
    QString result;
    qsizetype chunkStart = 0;
    for (qsizetype index = 0; index + 3 < bytes.size(); ++index) {
        const auto first = static_cast<quint8>(bytes.at(index));
        const auto second = static_cast<quint8>(bytes.at(index + 1));
        const auto third = static_cast<quint8>(bytes.at(index + 2));
        const auto fourth = static_cast<quint8>(bytes.at(index + 3));
        if (first < 0x81 || first > 0xfe || second < 0x30 || second > 0x39 || third < 0x81 ||
            third > 0xfe || fourth < 0x30 || fourth > 0x39)
            continue;
        const quint32 pointer =
            (((first - 0x81) * 10 + second - 0x30) * 126 + third - 0x81) * 10 + fourth - 0x30;
        if (pointer < 189000 || pointer > 1237575)
            continue;
        result += decodeWithCodec(bytes.sliced(chunkStart, index - chunkStart), codec, hadErrors);
        const char32_t codePoint = pointer - 189000 + 0x10000;
        result += QString::fromUcs4(&codePoint, 1);
        index += 3;
        chunkStart = index + 1;
    }
    result += decodeWithCodec(bytes.sliced(chunkStart), codec, hadErrors);
    return result;
}

} // namespace

std::optional<TextEncoding> textEncodingFromByteOrderMark(const QByteArray& bytes) {
    if (bytes.startsWith(QByteArrayLiteral("\xEF\xBB\xBF")))
        return TextEncoding::Utf8Bom;
    if (bytes.startsWith(QByteArrayLiteral("\xFF\xFE")))
        return TextEncoding::Utf16Le;
    if (bytes.startsWith(QByteArrayLiteral("\xFE\xFF")))
        return TextEncoding::Utf16Be;
    return std::nullopt;
}

std::optional<TextEncoding> guessTextEncoding(QByteArrayView bytes) {
    const QByteArrayView sample = bytes.first(qMin<qsizetype>(bytes.size(), 64 * 1024));
    if (sample.isEmpty())
        return std::nullopt;
    uchardet_t detector = uchardet_new();
    if (!detector)
        return std::nullopt;
    const int status = uchardet_handle_data(detector, sample.data(), sample.size());
    uchardet_data_end(detector);
    const QByteArray detected =
        status == 0 ? QByteArray(uchardet_get_charset(detector)) : QByteArray{};
    uchardet_delete(detector);

    const QString name = normalizedEncodingName(detected);
    if (name.isEmpty() || name == QStringLiteral("ascii") || name == QStringLiteral("utf16") ||
        name == QStringLiteral("utf32"))
        return std::nullopt;
    // uchardet reports Korean EUC-family text as UHC (Windows CP949), while LiteCode and
    // VS Code expose that compatible codec under the user-facing EUC-KR identifier.
    if (name == QStringLiteral("uhc") || name == QStringLiteral("cp949") ||
        name == QStringLiteral("windows949") || name == QStringLiteral("xwindows949"))
        return TextEncoding::EucKr;
    if (name == QStringLiteral("ibm866"))
        return TextEncoding::Cp866;
    if (name.startsWith(QStringLiteral("ibm")))
        return textEncodingFromKey(QStringLiteral("cp") + name.sliced(3));
    if (name == QStringLiteral("big5"))
        return TextEncoding::Cp950;
    return textEncodingFromKey(name);
}

bool decodeText(const QByteArray& bytes, TextEncoding fallbackEncoding, bool honorByteOrderMark,
                QByteArray* utf8, TextEncoding* actualEncoding, QString* errorMessage,
                bool* hadByteOrderMark, bool guessEncoding, bool* hadDecodingErrors) {
    if (!utf8 || !actualEncoding)
        return false;
    const std::optional<TextEncoding> bomEncoding = textEncodingFromByteOrderMark(bytes);
    if (hadByteOrderMark)
        *hadByteOrderMark = bomEncoding.has_value();
    ZeroByteDetection zeroBytes;
    if (bomEncoding != TextEncoding::Utf16Le && bomEncoding != TextEncoding::Utf16Be) {
        zeroBytes = detectUtf16OrBinary(bytes);
        if (zeroBytes.seemsBinary && honorByteOrderMark) {
            if (errorMessage)
                *errorMessage = QStringLiteral("The file appears to be binary.");
            return false;
        }
    }

    TextEncoding encoding = fallbackEncoding;
    if (honorByteOrderMark) {
        if (bomEncoding)
            encoding = *bomEncoding;
        else if (zeroBytes.encoding)
            encoding = *zeroBytes.encoding;
        else if (guessEncoding)
            encoding = guessTextEncoding(bytes).value_or(fallbackEncoding);
    } else if (fallbackEncoding == TextEncoding::Utf8 && bomEncoding == TextEncoding::Utf8Bom) {
        encoding = TextEncoding::Utf8Bom;
    }
    QByteArrayView payload(bytes);
    const QByteArray bom = byteOrderMark(encoding);
    if (!bom.isEmpty() && payload.startsWith(bom))
        payload = payload.sliced(bom.size());

    bool decodingErrors = false;
    QString decoded;
    if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::Utf8Bom) {
        decodingErrors = !isValidUtf8(payload.toByteArray());
        decoded = QString::fromUtf8(payload);
    } else if (encoding == TextEncoding::Utf16Le || encoding == TextEncoding::Utf16Be) {
        decoded = decodeUtf16(payload, encoding == TextEncoding::Utf16Le, &decodingErrors);
    } else if (const SingleByteTable* table = singleByteTable(encoding)) {
        decoded = decodeSingleByte(payload, *table, &decodingErrors);
    } else if (QTextCodec* codec = QTextCodec::codecForName(textEncodingCodecName(encoding))) {
        decoded = encoding == TextEncoding::Gb18030
                      ? decodeGb18030(payload, *codec, &decodingErrors)
                      : decodeWithCodec(payload, *codec, &decodingErrors);
    } else {
        if (errorMessage)
            *errorMessage = QStringLiteral("The selected text encoding is unavailable.");
        return false;
    }
    *utf8 = decoded.toUtf8();
    *actualEncoding = encoding;
    if (hadDecodingErrors)
        *hadDecodingErrors = decodingErrors;
    return true;
}

bool encodeText(const QByteArray& utf8, TextEncoding encoding, QByteArray* bytes,
                QString* errorMessage, std::optional<bool> includeByteOrderMark) {
    if (!bytes || !isValidUtf8(utf8)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("The editor contains invalid UTF-8 text.");
        return false;
    }
    const QString text = QString::fromUtf8(utf8);
    QByteArray encoded;
    bool lossless = true;
    if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::Utf8Bom)
        encoded = utf8;
    else if (encoding == TextEncoding::Utf16Le || encoding == TextEncoding::Utf16Be) {
        encoded.reserve(text.size() * 2);
        for (const QChar character : text) {
            const quint16 unit = character.unicode();
            encoded.append(
                static_cast<char>(encoding == TextEncoding::Utf16Le ? unit & 0xff : unit >> 8));
            encoded.append(
                static_cast<char>(encoding == TextEncoding::Utf16Le ? unit >> 8 : unit & 0xff));
        }
    } else if (const SingleByteTable* table = singleByteTable(encoding)) {
        encoded = encodeSingleByte(text, *table, &lossless);
    } else if (QTextCodec* codec = QTextCodec::codecForName(textEncodingCodecName(encoding))) {
        QTextCodec::ConverterState state;
        encoded = codec->fromUnicode(text.constData(), text.size(), &state);
        lossless = state.invalidChars == 0;
    } else {
        if (errorMessage)
            *errorMessage = QStringLiteral("The selected text encoding is unavailable.");
        return false;
    }
    if (!lossless) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The selected encoding cannot represent every character. Choose UTF-8 or "
                "another compatible encoding to save without data loss.");
        }
        return false;
    }
    if (encoding != TextEncoding::Utf8 && encoding != TextEncoding::Utf8Bom &&
        encoding != TextEncoding::Utf16Le && encoding != TextEncoding::Utf16Be) {
        QByteArray verifiedUtf8;
        TextEncoding verifiedEncoding{};
        bool verificationErrors = false;
        if (!decodeText(encoded, encoding, false, &verifiedUtf8, &verifiedEncoding, nullptr,
                        nullptr, false, &verificationErrors) ||
            verificationErrors || verifiedUtf8 != utf8) {
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "The selected encoding cannot preserve every character exactly. Choose "
                    "UTF-8 or another compatible encoding.");
            }
            return false;
        }
    }
    const bool writeBom = includeByteOrderMark.value_or(encoding == TextEncoding::Utf8Bom ||
                                                        encoding == TextEncoding::Utf16Le ||
                                                        encoding == TextEncoding::Utf16Be);
    if (writeBom)
        encoded.prepend(byteOrderMark(encoding));
    *bytes = std::move(encoded);
    return true;
}

QString ripgrepEncodingName(TextEncoding encoding) {
    const QString key = textEncodingKey(encoding);
    if (key == QStringLiteral("utf8") || key == QStringLiteral("utf8bom"))
        return QStringLiteral("utf-8");
    if (key == QStringLiteral("utf16le"))
        return QStringLiteral("utf-16le");
    if (key == QStringLiteral("utf16be"))
        return QStringLiteral("utf-16be");
    if (key.startsWith(QStringLiteral("windows")))
        return QStringLiteral("windows-") + key.sliced(7);
    if (key.startsWith(QStringLiteral("iso8859")))
        return QStringLiteral("iso-8859-") + key.sliced(7);
    if (key == QStringLiteral("macroman"))
        return QStringLiteral("macintosh");
    if (key == QStringLiteral("cp950"))
        return QStringLiteral("big5");
    if (key == QStringLiteral("shiftjis"))
        return QStringLiteral("shift-jis");
    if (key == QStringLiteral("big5hkscs"))
        return QStringLiteral("big5-hkscs");
    if (key == QStringLiteral("eucjp"))
        return QStringLiteral("euc-jp");
    if (key == QStringLiteral("euckr"))
        return QStringLiteral("euc-kr");
    if (key.startsWith(QStringLiteral("koi8")))
        return QStringLiteral("koi8-") + key.sliced(4);
    if (key.startsWith(QStringLiteral("cp")))
        return QStringLiteral("ibm") + key.sliced(2);
    return key;
}

} // namespace litecode::core
