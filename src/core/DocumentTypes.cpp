#include "core/DocumentTypes.h"

#include <array>
#include <limits>

namespace litecode::core {
namespace {

struct EncodingEntry final {
    TextEncoding encoding;
    const char* key;
    const char* longName;
    const char* shortName;
    const char* codecName;
    bool encodeOnly{};
};

// Keep this order aligned with VS Code's SUPPORTED_ENCODINGS catalog. The enum's first five
// numeric values are intentionally unchanged so existing LiteCode settings remain compatible.
constexpr std::array encodingCatalog{
    EncodingEntry{TextEncoding::Utf8, "utf8", "UTF-8", "UTF-8", "UTF-8"},
    EncodingEntry{TextEncoding::Utf8Bom, "utf8bom", "UTF-8 with BOM", "UTF-8 with BOM", "UTF-8",
                  true},
    EncodingEntry{TextEncoding::Utf16Le, "utf16le", "UTF-16 LE", "UTF-16 LE", "UTF-16LE"},
    EncodingEntry{TextEncoding::Utf16Be, "utf16be", "UTF-16 BE", "UTF-16 BE", "UTF-16BE"},
    EncodingEntry{TextEncoding::Windows1252, "windows1252", "Western (Windows 1252)",
                  "Windows 1252", "Windows-1252"},
    EncodingEntry{TextEncoding::Iso88591, "iso88591", "Western (ISO 8859-1)", "ISO 8859-1",
                  "ISO-8859-1"},
    EncodingEntry{TextEncoding::Iso88593, "iso88593", "Western (ISO 8859-3)", "ISO 8859-3",
                  "ISO-8859-3"},
    EncodingEntry{TextEncoding::Iso885915, "iso885915", "Western (ISO 8859-15)", "ISO 8859-15",
                  "ISO-8859-15"},
    EncodingEntry{TextEncoding::MacRoman, "macroman", "Western (Mac Roman)", "Mac Roman",
                  "Apple Roman"},
    EncodingEntry{TextEncoding::Cp437, "cp437", "DOS (CP 437)", "CP437", "IBM 437"},
    EncodingEntry{TextEncoding::Windows1256, "windows1256", "Arabic (Windows 1256)", "Windows 1256",
                  "Windows-1256"},
    EncodingEntry{TextEncoding::Iso88596, "iso88596", "Arabic (ISO 8859-6)", "ISO 8859-6",
                  "ISO-8859-6"},
    EncodingEntry{TextEncoding::Windows1257, "windows1257", "Baltic (Windows 1257)", "Windows 1257",
                  "Windows-1257"},
    EncodingEntry{TextEncoding::Iso88594, "iso88594", "Baltic (ISO 8859-4)", "ISO 8859-4",
                  "ISO-8859-4"},
    EncodingEntry{TextEncoding::Iso885914, "iso885914", "Celtic (ISO 8859-14)", "ISO 8859-14",
                  "ISO-8859-14"},
    EncodingEntry{TextEncoding::Windows1250, "windows1250", "Central European (Windows 1250)",
                  "Windows 1250", "Windows-1250"},
    EncodingEntry{TextEncoding::Iso88592, "iso88592", "Central European (ISO 8859-2)", "ISO 8859-2",
                  "ISO-8859-2"},
    EncodingEntry{TextEncoding::Cp852, "cp852", "Central European (CP 852)", "CP 852", "IBM 852"},
    EncodingEntry{TextEncoding::Windows1251, "windows1251", "Cyrillic (Windows 1251)",
                  "Windows 1251", "Windows-1251"},
    EncodingEntry{TextEncoding::Cp866, "cp866", "Cyrillic (CP 866)", "CP 866", "IBM 866"},
    EncodingEntry{TextEncoding::Cp1125, "cp1125", "Cyrillic (CP 1125)", "CP 1125", "IBM 1125"},
    EncodingEntry{TextEncoding::Iso88595, "iso88595", "Cyrillic (ISO 8859-5)", "ISO 8859-5",
                  "ISO-8859-5"},
    EncodingEntry{TextEncoding::Koi8R, "koi8r", "Cyrillic (KOI8-R)", "KOI8-R", "KOI8-R"},
    EncodingEntry{TextEncoding::Koi8U, "koi8u", "Cyrillic (KOI8-U)", "KOI8-U", "KOI8-U"},
    EncodingEntry{TextEncoding::Iso885913, "iso885913", "Estonian (ISO 8859-13)", "ISO 8859-13",
                  "ISO-8859-13"},
    EncodingEntry{TextEncoding::Windows1253, "windows1253", "Greek (Windows 1253)", "Windows 1253",
                  "Windows-1253"},
    EncodingEntry{TextEncoding::Iso88597, "iso88597", "Greek (ISO 8859-7)", "ISO 8859-7",
                  "ISO-8859-7"},
    EncodingEntry{TextEncoding::Windows1255, "windows1255", "Hebrew (Windows 1255)", "Windows 1255",
                  "Windows-1255"},
    EncodingEntry{TextEncoding::Iso88598, "iso88598", "Hebrew (ISO 8859-8)", "ISO 8859-8",
                  "ISO-8859-8"},
    EncodingEntry{TextEncoding::Iso885910, "iso885910", "Nordic (ISO 8859-10)", "ISO 8859-10",
                  "ISO-8859-10"},
    EncodingEntry{TextEncoding::Iso885916, "iso885916", "Romanian (ISO 8859-16)", "ISO 8859-16",
                  "ISO-8859-16"},
    EncodingEntry{TextEncoding::Windows1254, "windows1254", "Turkish (Windows 1254)",
                  "Windows 1254", "Windows-1254"},
    EncodingEntry{TextEncoding::Iso88599, "iso88599", "Turkish (ISO 8859-9)", "ISO 8859-9",
                  "ISO-8859-9"},
    EncodingEntry{TextEncoding::Cp857, "cp857", "Turkish (CP 857)", "CP 857", "IBM 857"},
    EncodingEntry{TextEncoding::Windows1258, "windows1258", "Vietnamese (Windows 1258)",
                  "Windows 1258", "Windows-1258"},
    EncodingEntry{TextEncoding::Gbk, "gbk", "Simplified Chinese (GBK)", "GBK", "GBK"},
    EncodingEntry{TextEncoding::Gb18030, "gb18030", "Simplified Chinese (GB18030)", "GB18030",
                  "GB18030"},
    EncodingEntry{TextEncoding::Cp950, "cp950", "Traditional Chinese (Big5)", "Big5", "Big5"},
    EncodingEntry{TextEncoding::Big5Hkscs, "big5hkscs", "Traditional Chinese (Big5-HKSCS)",
                  "Big5-HKSCS", "Big5-HKSCS"},
    EncodingEntry{TextEncoding::ShiftJis, "shiftjis", "Japanese (Shift JIS)", "Shift JIS",
                  "Shift-JIS"},
    EncodingEntry{TextEncoding::EucJp, "eucjp", "Japanese (EUC-JP)", "EUC-JP", "EUC-JP"},
    EncodingEntry{TextEncoding::EucKr, "euckr", "Korean (EUC-KR)", "EUC-KR", "EUC-KR"},
    EncodingEntry{TextEncoding::Windows874, "windows874", "Thai (Windows 874)", "Windows 874",
                  "IBM 874"},
    EncodingEntry{TextEncoding::Iso885911, "iso885911", "Latin/Thai (ISO 8859-11)", "ISO 8859-11",
                  "ISO-8859-11"},
    EncodingEntry{TextEncoding::Koi8Ru, "koi8ru", "Cyrillic (KOI8-RU)", "KOI8-RU", "KOI8-RU"},
    EncodingEntry{TextEncoding::Koi8T, "koi8t", "Tajik (KOI8-T)", "KOI8-T", "KOI8-T"},
    EncodingEntry{TextEncoding::Gb2312, "gb2312", "Simplified Chinese (GB 2312)", "GB 2312",
                  "GB2312"},
    EncodingEntry{TextEncoding::Cp865, "cp865", "Nordic DOS (CP 865)", "CP 865", "IBM 865"},
    EncodingEntry{TextEncoding::Cp850, "cp850", "Western European DOS (CP 850)", "CP 850",
                  "IBM 850"},
};

const EncodingEntry& entryFor(TextEncoding encoding) {
    for (const EncodingEntry& entry : encodingCatalog) {
        if (entry.encoding == encoding)
            return entry;
    }
    return encodingCatalog.front();
}

bool codecAvailable(const EncodingEntry&) { return true; }

} // namespace

QString textEncodingDisplayName(TextEncoding encoding) {
    return QString::fromLatin1(entryFor(encoding).shortName);
}

QString textEncodingLongDisplayName(TextEncoding encoding) {
    return QString::fromLatin1(entryFor(encoding).longName);
}

QString textEncodingKey(TextEncoding encoding) {
    return QString::fromLatin1(entryFor(encoding).key);
}

QByteArray textEncodingCodecName(TextEncoding encoding) {
    return QByteArray(entryFor(encoding).codecName);
}

std::optional<TextEncoding> textEncodingFromKey(const QString& key) {
    for (const EncodingEntry& entry : encodingCatalog) {
        if (key.compare(QString::fromLatin1(entry.key), Qt::CaseInsensitive) == 0 &&
            codecAvailable(entry))
            return entry.encoding;
    }
    return std::nullopt;
}

QVector<TextEncoding> supportedTextEncodings() {
    QVector<TextEncoding> encodings;
    encodings.reserve(static_cast<qsizetype>(encodingCatalog.size()));
    for (const EncodingEntry& entry : encodingCatalog) {
        if (codecAvailable(entry))
            encodings.append(entry.encoding);
    }
    return encodings;
}

QVector<TextEncoding> supportedTextDecodingEncodings() {
    QVector<TextEncoding> encodings;
    encodings.reserve(static_cast<qsizetype>(encodingCatalog.size() - 1));
    for (const EncodingEntry& entry : encodingCatalog) {
        if (!entry.encodeOnly && codecAvailable(entry))
            encodings.append(entry.encoding);
    }
    return encodings;
}

DocumentId DocumentIdGenerator::next() noexcept {
    if (next_ == 0) {
        return {};
    }
    const DocumentId result(next_);
    if (next_ == std::numeric_limits<quint64>::max()) {
        next_ = 0;
    } else {
        ++next_;
    }
    return result;
}

} // namespace litecode::core
