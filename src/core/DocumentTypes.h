#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtTypes>

#include <optional>

namespace litecode::core {

class DocumentId final {
  public:
    constexpr DocumentId() noexcept = default;
    explicit constexpr DocumentId(quint64 value) noexcept : value_(value) {}

    [[nodiscard]] constexpr quint64 value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(DocumentId, DocumentId) noexcept = default;

  private:
    quint64 value_{};
};

class DocumentIdGenerator final {
  public:
    [[nodiscard]] DocumentId next() noexcept;

  private:
    quint64 next_{1};
};

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Windows1252,
    Iso88591,
    Iso88593,
    Iso885915,
    MacRoman,
    Cp437,
    Windows1256,
    Iso88596,
    Windows1257,
    Iso88594,
    Iso885914,
    Windows1250,
    Iso88592,
    Cp852,
    Windows1251,
    Cp866,
    Cp1125,
    Iso88595,
    Koi8R,
    Koi8U,
    Iso885913,
    Windows1253,
    Iso88597,
    Windows1255,
    Iso88598,
    Iso885910,
    Iso885916,
    Windows1254,
    Iso88599,
    Cp857,
    Windows1258,
    Gbk,
    Gb18030,
    Cp950,
    Big5Hkscs,
    ShiftJis,
    EucJp,
    EucKr,
    Windows874,
    Iso885911,
    Koi8Ru,
    Koi8T,
    Gb2312,
    Cp865,
    Cp850,
};

[[nodiscard]] QString textEncodingDisplayName(TextEncoding encoding);
[[nodiscard]] QString textEncodingLongDisplayName(TextEncoding encoding);
[[nodiscard]] QString textEncodingKey(TextEncoding encoding);
[[nodiscard]] QByteArray textEncodingCodecName(TextEncoding encoding);
[[nodiscard]] std::optional<TextEncoding> textEncodingFromKey(const QString& key);
[[nodiscard]] QVector<TextEncoding> supportedTextEncodings();
[[nodiscard]] QVector<TextEncoding> supportedTextDecodingEncodings();

enum class LineEnding {
    None,
    Lf,
    CrLf,
    Cr,
    Mixed,
};

enum class DocumentStorageMode {
    Normal,
    Large,
};

struct TextPosition final {
    int line{};
    int utf16Column{};
};

struct TextRange final {
    TextPosition start;
    TextPosition end;
};

struct DocumentEdit final {
    DocumentId documentId;
    QString filePath;
    qint64 position{};
    qint64 removedLength{};
    QByteArray insertedText;
    TextRange range;
    qint64 version{};
};

struct DocumentPathChange final {
    DocumentId documentId;
    QString oldPath;
    QString newPath;
    qint64 version{};
};

} // namespace litecode::core

Q_DECLARE_METATYPE(litecode::core::DocumentId)
Q_DECLARE_METATYPE(litecode::core::TextEncoding)
Q_DECLARE_METATYPE(litecode::core::TextPosition)
Q_DECLARE_METATYPE(litecode::core::TextRange)
Q_DECLARE_METATYPE(litecode::core::DocumentEdit)
Q_DECLARE_METATYPE(litecode::core::DocumentPathChange)
