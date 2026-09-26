#include "core/TextEncoding.h"
#include "editor/DocumentSession.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using litecode::core::DocumentId;
using litecode::core::TextEncoding;
using litecode::editor::DocumentLoadResult;
using litecode::editor::DocumentSession;

namespace {

bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class DocumentEncodingTest final : public QObject {
    Q_OBJECT

  private slots:
    void exposesVsCodeEncodingCatalog();
    void roundTripsRepresentativeEncodingFamilies();
    void detectsUnicodeByteOrderMarks();
    void opensAndPreservesWindows1252();
    void savesUtf16WithByteOrderMark();
    void refusesLossyLegacySave();
    void rejectsUnrepresentableCharactersAcrossLegacyCatalog();
    void opensMalformedUtf8WithReplacement();
    void refusesToOverwriteMalformedSourceBytes();
    void handlesMalformedUtf8FixtureSafely();
    void refusesToOverwriteExternalChanges();
    void allowsNulAfterExplicitDecode();
    void detectsUtf16WithoutByteOrderMark();
    void addsBomToUtf16AcrossSaveAndSaveAs();
    void handlesBomPrecedenceAndWrongExplicitEncoding();
    void matchesVsCodeUtf32BomFallback();
    void autoGuessesEncodingWhenEnabled();
    void autoGuessesShortEucKrText();
    void handlesEmptyAndInvalidBomPayloads();
    void roundTripsNonLatinTextAndReplacesMalformedUtf16();
    void preservesUtf8BomWhenExplicitlyDecoded();
    void appliesUtf8BomDefaultOnNormalSave();
    void honorsBomWhenReloadUsesPreviousEncodingAsFallback();
    void preservesExplicitEncodingAcrossReload();
    void choosesPredominantLineEnding();
    void deletionDuringLoadRemainsVisible();
};

void DocumentEncodingTest::choosesPredominantLineEnding() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString crlfPath = directory.filePath(QStringLiteral("crlf.txt"));
    const QString lfPath = directory.filePath(QStringLiteral("lf.txt"));
    const QString tiedPath = directory.filePath(QStringLiteral("tied.txt"));
    QVERIFY(writeBytes(crlfPath, QByteArrayLiteral("one\r\ntwo\r\nthree\n")));
    QVERIFY(writeBytes(lfPath, QByteArrayLiteral("one\ntwo\nthree\r\n")));
    QVERIFY(writeBytes(tiedPath, QByteArrayLiteral("one\r\ntwo\n")));

    const DocumentLoadResult crlf = DocumentSession::loadBounded(crlfPath);
    QCOMPARE(crlf.lineEnding, litecode::core::LineEnding::CrLf);
    QCOMPARE(crlf.contents, QByteArrayLiteral("one\r\ntwo\r\nthree\r\n"));
    const DocumentLoadResult lf = DocumentSession::loadBounded(lfPath);
    QCOMPARE(lf.lineEnding, litecode::core::LineEnding::Lf);
    QCOMPARE(lf.contents, QByteArrayLiteral("one\ntwo\nthree\n"));
    const DocumentLoadResult tied = DocumentSession::loadBounded(tiedPath);
    QCOMPARE(tied.lineEnding, litecode::core::LineEnding::Lf);
    QCOMPARE(tied.contents, QByteArrayLiteral("one\ntwo\n"));
}

void DocumentEncodingTest::deletionDuringLoadRemainsVisible() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("loading.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("contents")));
    const DocumentLoadResult load = DocumentSession::loadBounded(path);

    DocumentSession session(DocumentId(1), path);
    session.markDeleted();
    QVERIFY(session.acceptLoad(load));
    QVERIFY(session.isDeleted());
}

void DocumentEncodingTest::exposesVsCodeEncodingCatalog() {
    const QStringList expectedKeys{
        QStringLiteral("utf8"),        QStringLiteral("utf8bom"),     QStringLiteral("utf16le"),
        QStringLiteral("utf16be"),     QStringLiteral("windows1252"), QStringLiteral("iso88591"),
        QStringLiteral("iso88593"),    QStringLiteral("iso885915"),   QStringLiteral("macroman"),
        QStringLiteral("cp437"),       QStringLiteral("windows1256"), QStringLiteral("iso88596"),
        QStringLiteral("windows1257"), QStringLiteral("iso88594"),    QStringLiteral("iso885914"),
        QStringLiteral("windows1250"), QStringLiteral("iso88592"),    QStringLiteral("cp852"),
        QStringLiteral("windows1251"), QStringLiteral("cp866"),       QStringLiteral("cp1125"),
        QStringLiteral("iso88595"),    QStringLiteral("koi8r"),       QStringLiteral("koi8u"),
        QStringLiteral("iso885913"),   QStringLiteral("windows1253"), QStringLiteral("iso88597"),
        QStringLiteral("windows1255"), QStringLiteral("iso88598"),    QStringLiteral("iso885910"),
        QStringLiteral("iso885916"),   QStringLiteral("windows1254"), QStringLiteral("iso88599"),
        QStringLiteral("cp857"),       QStringLiteral("windows1258"), QStringLiteral("gbk"),
        QStringLiteral("gb18030"),     QStringLiteral("cp950"),       QStringLiteral("big5hkscs"),
        QStringLiteral("shiftjis"),    QStringLiteral("eucjp"),       QStringLiteral("euckr"),
        QStringLiteral("windows874"),  QStringLiteral("iso885911"),   QStringLiteral("koi8ru"),
        QStringLiteral("koi8t"),       QStringLiteral("gb2312"),      QStringLiteral("cp865"),
        QStringLiteral("cp850"),
    };
    const QVector<TextEncoding> encodings = litecode::core::supportedTextEncodings();
    QStringList actualKeys;
    for (const TextEncoding encoding : encodings)
        actualKeys.append(litecode::core::textEncodingKey(encoding));

    QStringList missing = expectedKeys;
    for (const QString& key : actualKeys)
        missing.removeAll(key);
    QVERIFY2(
        missing.isEmpty(),
        qPrintable(
            QStringLiteral("Unavailable Qt codecs: %1").arg(missing.join(QStringLiteral(", ")))));
    QCOMPARE(actualKeys, expectedKeys);
    QCOMPARE(litecode::core::supportedTextDecodingEncodings().size(), expectedKeys.size() - 1);

    QStringList unavailable;
    for (const TextEncoding encoding : encodings) {
        const QString key = litecode::core::textEncodingKey(encoding);
        QCOMPARE(litecode::core::textEncodingFromKey(key), std::optional{encoding});
        QByteArray encoded;
        QString error;
        if (!litecode::core::encodeText(QByteArrayLiteral("ASCII"), encoding, &encoded, &error)) {
            unavailable.append(key + QStringLiteral(": ") + error);
            continue;
        }
        QByteArray decoded;
        TextEncoding actual{};
        const TextEncoding decodingEncoding =
            encoding == TextEncoding::Utf8Bom ? TextEncoding::Utf8 : encoding;
        if (!litecode::core::decodeText(encoded, decodingEncoding, false, &decoded, &actual,
                                        &error)) {
            unavailable.append(key + QStringLiteral(": ") + error);
            continue;
        }
        QCOMPARE(decoded, QByteArrayLiteral("ASCII"));
    }
    QVERIFY2(unavailable.isEmpty(), qPrintable(unavailable.join(QStringLiteral("; "))));
}

void DocumentEncodingTest::roundTripsRepresentativeEncodingFamilies() {
    struct Case final {
        TextEncoding encoding;
        QString text;
    };
    const QVector<Case> cases{
        {TextEncoding::Iso885915, QStringLiteral("café €")},
        {TextEncoding::MacRoman, QStringLiteral("café")},
        {TextEncoding::Cp437, QStringLiteral("Çα")},
        {TextEncoding::Windows1256, QStringLiteral("مرحبا")},
        {TextEncoding::Windows1257, QStringLiteral("Āžuolas")},
        {TextEncoding::Iso885914, QStringLiteral("ŵ ŷ")},
        {TextEncoding::Cp852, QStringLiteral("Łódź")},
        {TextEncoding::Windows1251, QStringLiteral("Привет")},
        {TextEncoding::Cp866, QStringLiteral("Привет")},
        {TextEncoding::Cp1125, QStringLiteral("Ґрунт")},
        {TextEncoding::Koi8T, QStringLiteral("Тоҷикӣ")},
        {TextEncoding::Windows1253, QStringLiteral("Ελλάδα")},
        {TextEncoding::Windows1255, QStringLiteral("שלום")},
        {TextEncoding::Windows1254, QStringLiteral("Türkçe")},
        {TextEncoding::Windows1258, QStringLiteral("Viê\u0323t")},
        {TextEncoding::Gbk, QStringLiteral("中国")},
        {TextEncoding::Gb18030, QStringLiteral("中国 😀")},
        {TextEncoding::Cp950, QStringLiteral("中文")},
        {TextEncoding::Big5Hkscs, QStringLiteral("香港")},
        {TextEncoding::ShiftJis, QStringLiteral("日本語")},
        {TextEncoding::EucJp, QStringLiteral("日本語")},
        {TextEncoding::EucKr, QStringLiteral("한국어")},
        {TextEncoding::Windows874, QStringLiteral("ภาษาไทย")},
        {TextEncoding::Iso885911, QStringLiteral("\u0e20\u0e32\u0e29\u0e32\u0e44\u0e17\u0e22")},
        {TextEncoding::Koi8Ru, QStringLiteral("\u2116 \u045e")},
        {TextEncoding::Cp857, QStringLiteral("Türkçe")},
        {TextEncoding::Cp865, QStringLiteral("blåbær")},
        {TextEncoding::Cp850, QStringLiteral("café")},
    };

    for (const Case& item : cases) {
        QByteArray encoded;
        QString error;
        QVERIFY2(litecode::core::encodeText(item.text.toUtf8(), item.encoding, &encoded, &error),
                 qPrintable(litecode::core::textEncodingKey(item.encoding) + QStringLiteral(": ") +
                            error));
        if (item.encoding == TextEncoding::Gb18030)
            QCOMPARE(encoded.sliced(encoded.size() - 4).toHex(), QByteArrayLiteral("9439fc36"));
        QByteArray decoded;
        TextEncoding actual{};
        QVERIFY2(
            litecode::core::decodeText(encoded, item.encoding, false, &decoded, &actual, &error),
            qPrintable(litecode::core::textEncodingKey(item.encoding) + QStringLiteral(": ") +
                       error));
        QCOMPARE(QString::fromUtf8(decoded), item.text);
        QCOMPARE(actual, item.encoding);
    }
}

void DocumentEncodingTest::detectsUnicodeByteOrderMarks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    struct Case final {
        QString name;
        QByteArray bytes;
        TextEncoding encoding;
    };
    const QVector<Case> cases{
        {QStringLiteral("utf8.txt"), QByteArray::fromHex("EFBBBF68C3A90A"), TextEncoding::Utf8Bom},
        {QStringLiteral("utf16le.txt"), QByteArray::fromHex("FFFE6800E9000A00"),
         TextEncoding::Utf16Le},
        {QStringLiteral("utf16be.txt"), QByteArray::fromHex("FEFF006800E9000A"),
         TextEncoding::Utf16Be},
    };

    for (const Case& item : cases) {
        const QString path = directory.filePath(item.name);
        QVERIFY(writeBytes(path, item.bytes));
        const DocumentLoadResult load = DocumentSession::loadBounded(path);
        QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
        QCOMPARE(load.encoding, item.encoding);
        QCOMPARE(load.contents, QStringLiteral("hé\n").toUtf8());
    }

    const DocumentLoadResult explicitUtf8 = DocumentSession::loadBounded(
        directory.filePath(QStringLiteral("utf8.txt")), TextEncoding::Utf8);
    QCOMPARE(explicitUtf8.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(explicitUtf8.encoding, TextEncoding::Utf8Bom);
    QCOMPARE(explicitUtf8.requestedEncoding, std::optional{TextEncoding::Utf8Bom});
    QCOMPARE(explicitUtf8.contents, QStringLiteral("hé\n").toUtf8());
}

void DocumentEncodingTest::opensAndPreservesWindows1252() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("legacy.txt"));
    const QByteArray original = QByteArray::fromHex("636166E920800A");
    QVERIFY(writeBytes(path, original));

    const DocumentLoadResult load = DocumentSession::loadBounded(path, TextEncoding::Windows1252);
    QVERIFY2(load.outcome == DocumentLoadResult::Outcome::Loaded, qPrintable(load.diagnostic));
    QCOMPARE(load.encoding, TextEncoding::Windows1252);
    QCOMPARE(load.contents, QStringLiteral("café €\n").toUtf8());

    DocumentSession session(DocumentId(1), path);
    QVERIFY(session.acceptLoad(load));
    QString error;
    QVERIFY2(session.save(load.contents, &error), qPrintable(error));
    QCOMPARE(readBytes(path), original);
}

void DocumentEncodingTest::savesUtf16WithByteOrderMark() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("unicode.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("initial")));

    const DocumentLoadResult load = DocumentSession::loadBounded(path);
    DocumentSession session(DocumentId(2), path);
    QVERIFY(session.acceptLoad(load));
    QString error;
    QVERIFY2(
        session.saveWithEncoding(QStringLiteral("hé\n").toUtf8(), TextEncoding::Utf16Le, &error),
        qPrintable(error));
    QCOMPARE(session.encoding(), TextEncoding::Utf16Le);
    QCOMPARE(readBytes(path), QByteArray::fromHex("FFFE6800E9000A00"));
}

void DocumentEncodingTest::refusesLossyLegacySave() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("safe.txt"));
    const QByteArray original = QByteArrayLiteral("unchanged");
    QVERIFY(writeBytes(path, original));

    const DocumentLoadResult load = DocumentSession::loadBounded(path);
    DocumentSession session(DocumentId(3), path);
    QVERIFY(session.acceptLoad(load));
    QString error;
    QVERIFY(!session.saveWithEncoding(QStringLiteral("emoji 😀").toUtf8(),
                                      TextEncoding::Windows1252, &error));
    QVERIFY(error.contains(QStringLiteral("without data loss")));
    QCOMPARE(readBytes(path), original);
    QCOMPARE(session.encoding(), TextEncoding::Utf8);
}

void DocumentEncodingTest::rejectsUnrepresentableCharactersAcrossLegacyCatalog() {
    const QVector<TextEncoding> unicodeEncodings{TextEncoding::Utf8, TextEncoding::Utf8Bom,
                                                 TextEncoding::Utf16Le, TextEncoding::Utf16Be,
                                                 TextEncoding::Gb18030};
    const QByteArray text = QStringLiteral("safe 😀").toUtf8();
    for (const TextEncoding encoding : litecode::core::supportedTextEncodings()) {
        QByteArray encoded;
        QString error;
        const bool succeeded = litecode::core::encodeText(text, encoding, &encoded, &error);
        if (unicodeEncodings.contains(encoding)) {
            QVERIFY2(succeeded, qPrintable(litecode::core::textEncodingKey(encoding) +
                                           QStringLiteral(": ") + error));
        } else {
            QVERIFY2(!succeeded, qPrintable(litecode::core::textEncodingKey(encoding)));
            QVERIFY(!error.isEmpty());
        }
    }
}

void DocumentEncodingTest::opensMalformedUtf8WithReplacement() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("unknown.txt"));
    QVERIFY(writeBytes(path, QByteArray::fromHex("82A082A2")));

    const DocumentLoadResult load = DocumentSession::loadBounded(path);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(load.encoding, TextEncoding::Utf8);
    QCOMPARE(QString::fromUtf8(load.contents), QString(4, QChar::ReplacementCharacter));
    QVERIFY(load.hadDecodingErrors);
}

void DocumentEncodingTest::refusesToOverwriteMalformedSourceBytes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("malformed.txt"));
    const QByteArray original = QByteArray::fromHex("ff68656c6c6f");
    QVERIFY(writeBytes(path, original));

    const DocumentLoadResult load = DocumentSession::loadBounded(path);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    QVERIFY(load.hadDecodingErrors);
    DocumentSession session(DocumentId(40), path);
    QVERIFY(session.acceptLoad(load));
    QString error;
    QVERIFY(!session.save(load.contents + QByteArrayLiteral(" edited"), &error));
    QVERIFY(error.contains(QStringLiteral("could not be decoded safely")));
    QCOMPARE(readBytes(path), original);

    QVERIFY(!session.saveAs(path, load.contents, nullptr, &error));
    QCOMPARE(readBytes(path), original);

    const QString recoveredPath = directory.filePath(QStringLiteral("recovered.txt"));
    QVERIFY2(session.saveAs(recoveredPath, load.contents, nullptr, &error), qPrintable(error));
    QCOMPARE(readBytes(path), original);
    QCOMPARE(readBytes(recoveredPath), load.contents);
    QVERIFY(!session.hadDecodingErrors());
}

void DocumentEncodingTest::handlesMalformedUtf8FixtureSafely() {
    const QByteArray original = QByteArray::fromHex(
        "496e74656e74696f6e616c6c79206d616c666f726d6564205554462d380d0ac3280d0a");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString explicitUtf8Path = directory.filePath(QStringLiteral("explicit-utf8.txt"));
    QVERIFY(writeBytes(explicitUtf8Path, original));
    const DocumentLoadResult explicitUtf8 =
        DocumentSession::loadBounded(explicitUtf8Path, TextEncoding::Utf8);
    QCOMPARE(explicitUtf8.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(explicitUtf8.encoding, TextEncoding::Utf8);
    QVERIFY(explicitUtf8.hadDecodingErrors);
    DocumentSession unsafeSession(DocumentId(41), explicitUtf8Path);
    QVERIFY(unsafeSession.acceptLoad(explicitUtf8));
    QString error;
    QVERIFY(!unsafeSession.save(explicitUtf8.contents + QByteArrayLiteral("edited"), &error));
    QVERIFY(error.contains(QStringLiteral("could not be decoded safely")));
    QCOMPARE(readBytes(explicitUtf8Path), original);

    const QString guessedPath = directory.filePath(QStringLiteral("auto-guessed.txt"));
    QVERIFY(writeBytes(guessedPath, original));
    const DocumentLoadResult guessed =
        DocumentSession::loadBounded(guessedPath, std::nullopt, TextEncoding::Utf8, true);
    QCOMPARE(guessed.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(guessed.encoding, TextEncoding::Windows1252);
    QVERIFY(!guessed.hadDecodingErrors);
    DocumentSession safeSession(DocumentId(42), guessedPath);
    QVERIFY(safeSession.acceptLoad(guessed));
    QVERIFY2(safeSession.save(guessed.contents + QByteArrayLiteral("edited"), &error),
             qPrintable(error));
    QCOMPARE(readBytes(guessedPath), original + QByteArrayLiteral("edited"));
}

void DocumentEncodingTest::refusesToOverwriteExternalChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("conflict.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("first")));

    const DocumentLoadResult load = DocumentSession::loadBounded(path);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    DocumentSession session(DocumentId(41), path);
    QVERIFY(session.acceptLoad(load));
    QVERIFY(writeBytes(path, QByteArrayLiteral("other"))); // Same size: hash must still detect it.

    QString error;
    QVERIFY(!session.save(QByteArrayLiteral("local"), &error));
    QVERIFY(error.contains(QStringLiteral("changed on disk")));
    QCOMPARE(readBytes(path), QByteArrayLiteral("other"));
}

void DocumentEncodingTest::allowsNulAfterExplicitDecode() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("binary.dat"));
    QVERIFY(writeBytes(path, QByteArray::fromHex("61626300646566")));

    const DocumentLoadResult load = DocumentSession::loadBounded(path, TextEncoding::Windows1252);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(load.encoding, TextEncoding::Windows1252);
    QCOMPARE(load.contents, QByteArrayLiteral("abc\0def"));
}

void DocumentEncodingTest::detectsUtf16WithoutByteOrderMark() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString littlePath = directory.filePath(QStringLiteral("utf16le.txt"));
    const QString bigPath = directory.filePath(QStringLiteral("utf16be.txt"));
    QVERIFY(writeBytes(littlePath, QByteArray::fromHex("680065006C006C006F00")));
    QVERIFY(writeBytes(bigPath, QByteArray::fromHex("00680065006C006C006F")));

    const DocumentLoadResult little = DocumentSession::loadBounded(littlePath);
    QCOMPARE(little.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(little.encoding, TextEncoding::Utf16Le);
    QCOMPARE(little.contents, QByteArrayLiteral("hello"));

    const DocumentLoadResult big = DocumentSession::loadBounded(bigPath);
    QCOMPARE(big.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(big.encoding, TextEncoding::Utf16Be);
    QCOMPARE(big.contents, QByteArrayLiteral("hello"));

    const QString mixedText = QStringLiteral("ab中文cd");
    QByteArray mixedLittle;
    QByteArray mixedBig;
    QVERIFY(litecode::core::encodeText(mixedText.toUtf8(), TextEncoding::Utf16Le, &mixedLittle,
                                       nullptr, false));
    QVERIFY(litecode::core::encodeText(mixedText.toUtf8(), TextEncoding::Utf16Be, &mixedBig,
                                       nullptr, false));
    const QString mixedLittlePath = directory.filePath(QStringLiteral("mixed-le.txt"));
    const QString mixedBigPath = directory.filePath(QStringLiteral("mixed-be.txt"));
    QVERIFY(writeBytes(mixedLittlePath, mixedLittle));
    QVERIFY(writeBytes(mixedBigPath, mixedBig));
    QCOMPARE(DocumentSession::loadBounded(mixedLittlePath).contents, mixedText.toUtf8());
    QCOMPARE(DocumentSession::loadBounded(mixedBigPath).contents, mixedText.toUtf8());
}

void DocumentEncodingTest::addsBomToUtf16AcrossSaveAndSaveAs() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    struct Case final {
        QString name;
        QByteArray original;
        QByteArray edited;
        TextEncoding encoding;
    };
    const QVector<Case> cases{
        {QStringLiteral("little.txt"), QByteArray::fromHex("680065006C006C006F00"),
         QByteArray::fromHex("77006F0072006C006400"), TextEncoding::Utf16Le},
        {QStringLiteral("big.txt"), QByteArray::fromHex("00680065006C006C006F"),
         QByteArray::fromHex("0077006F0072006C0064"), TextEncoding::Utf16Be},
    };

    quint64 documentId = 10;
    for (const Case& item : cases) {
        const QString path = directory.filePath(item.name);
        QVERIFY(writeBytes(path, item.original));
        const DocumentLoadResult load = DocumentSession::loadBounded(path);
        QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
        QCOMPARE(load.encoding, item.encoding);
        QVERIFY(!load.hasByteOrderMark);

        DocumentSession session(DocumentId(documentId++), path);
        QVERIFY(session.acceptLoad(load));
        QVERIFY(!session.hasByteOrderMark());
        QString error;
        QVERIFY2(session.save(QByteArrayLiteral("world"), &error), qPrintable(error));
        const QByteArray bom = item.encoding == TextEncoding::Utf16Le ? QByteArray::fromHex("FFFE")
                                                                      : QByteArray::fromHex("FEFF");
        QCOMPARE(readBytes(path), bom + item.edited);
        QVERIFY(session.hasByteOrderMark());

        const QString copyPath = directory.filePath(QStringLiteral("copy-") + item.name);
        QVERIFY2(session.saveAs(copyPath, QByteArrayLiteral("world"), nullptr, &error),
                 qPrintable(error));
        QCOMPARE(readBytes(copyPath), bom + item.edited);
    }
}

void DocumentEncodingTest::handlesBomPrecedenceAndWrongExplicitEncoding() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString utf16Path = directory.filePath(QStringLiteral("utf16.txt"));
    QVERIFY(writeBytes(utf16Path, QByteArray::fromHex("FFFE6800E9000A00")));

    const DocumentLoadResult automatic =
        DocumentSession::loadBounded(utf16Path, std::nullopt, TextEncoding::Windows1252);
    QCOMPARE(automatic.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(automatic.encoding, TextEncoding::Utf16Le);
    QVERIFY(automatic.hasByteOrderMark);
    QCOMPARE(automatic.contents, QStringLiteral("hé\n").toUtf8());

    const DocumentLoadResult explicitWindows1252 =
        DocumentSession::loadBounded(utf16Path, TextEncoding::Windows1252);
    QCOMPARE(explicitWindows1252.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(explicitWindows1252.encoding, TextEncoding::Windows1252);
    QVERIFY(explicitWindows1252.hasByteOrderMark);
    QVERIFY(!explicitWindows1252.writeByteOrderMark);
    QVERIFY(explicitWindows1252.contents.contains('\0'));

    const DocumentLoadResult forcedUtf8 =
        DocumentSession::loadBounded(utf16Path, TextEncoding::Utf8);
    QCOMPARE(forcedUtf8.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(forcedUtf8.encoding, TextEncoding::Utf8);
    QVERIFY(forcedUtf8.hasByteOrderMark);
    QVERIFY(!forcedUtf8.writeByteOrderMark);
    QVERIFY(forcedUtf8.contents.contains('\0'));

    const QString utf8Path = directory.filePath(QStringLiteral("utf8.txt"));
    const QByteArray utf8 = QStringLiteral("hé\n").toUtf8();
    QVERIFY(writeBytes(utf8Path, utf8));
    const DocumentLoadResult forcedUtf16 =
        DocumentSession::loadBounded(utf8Path, TextEncoding::Utf16Le);
    QCOMPARE(forcedUtf16.outcome, DocumentLoadResult::Outcome::Loaded);
    QVERIFY(forcedUtf16.contents != utf8);
    QCOMPARE(readBytes(utf8Path), utf8);
}

void DocumentEncodingTest::matchesVsCodeUtf32BomFallback() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("utf32-le.txt"));
    QVERIFY(writeBytes(path, QByteArray::fromHex("FFFE000041000000")));

    // VS Code has no UTF-32 decoder. Its BOM detection sees the leading UTF-16 LE BOM and the
    // UTF-16 decoder receives the remaining UTF-32 code units.
    const DocumentLoadResult load = DocumentSession::loadBounded(path);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(load.encoding, TextEncoding::Utf16Le);
    QCOMPARE(load.contents, QByteArray("\0A\0", 3));
}

void DocumentEncodingTest::autoGuessesEncodingWhenEnabled() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto verify = [&directory](const QString& name, const QString& text,
                                     TextEncoding sourceEncoding, TextEncoding expectedEncoding) {
        QByteArray encoded;
        QString error;
        QVERIFY2(litecode::core::encodeText(text.toUtf8(), sourceEncoding, &encoded, &error, false),
                 qPrintable(error));
        const QString path = directory.filePath(name);
        QVERIFY(writeBytes(path, encoded));
        const DocumentLoadResult load =
            DocumentSession::loadBounded(path, std::nullopt, TextEncoding::Utf8, true);
        QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
        QCOMPARE(load.encoding, expectedEncoding);
        QCOMPARE(load.contents, text.toUtf8());
    };

    verify(QStringLiteral("utf8.txt"),
           QStringLiteral("Zażółć gęślą jaźń — UTF-8 text ").repeated(8), TextEncoding::Utf8,
           TextEncoding::Utf8);
    verify(QStringLiteral("windows1252.txt"),
           QStringLiteral("using System;\nusing System.Drawing;\nusing System.Collections;\n"
                          "using System.ComponentModel;\nusing System.Windows.Forms;\n"
                          "using System.Data;\nusing System.Data.OleDb;\nusing System.Data.Odbc;\n"
                          "using System.IO;\nusing System.Net.Mail;\n"
                          "using System.Text.RegularExpressions;\n"
                          "using System.DirectoryServices;\nusing System.Diagnostics;\n"
                          "using System.Resources;\nusing System.Globalization;\n"
                          "using System.Reflection;\n"
                          "using System.Runtime.Serialization.Formatters.Binary;\n"
                          "using System.Runtime.Serialization;\n\n\n"
                          "ObjectCount = LoadObjects(\"Öffentlicher Ordner\");\n\n"
                          "Private = \"Persönliche Information — café €\"\n")
               .repeated(12),
           TextEncoding::Windows1252, TextEncoding::Windows1252);
    verify(QStringLiteral("shift-jis.txt"),
           QStringLiteral("日本語の文章を文字コード判定のために繰り返します。").repeated(12),
           TextEncoding::ShiftJis, TextEncoding::ShiftJis);

    const QString asciiPath = directory.filePath(QStringLiteral("ascii.txt"));
    QVERIFY(writeBytes(asciiPath, QByteArrayLiteral("plain ASCII remains configured encoding")));
    const DocumentLoadResult ascii =
        DocumentSession::loadBounded(asciiPath, std::nullopt, TextEncoding::Windows1252, true);
    QCOMPARE(ascii.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(ascii.encoding, TextEncoding::Windows1252);
}

void DocumentEncodingTest::autoGuessesShortEucKrText() {
    const QByteArray bytes = QByteArray::fromHex(
        "4555432d4b520d0abec8b3e7c7cfbcbcbfe42e20c7d1b1b9beee20c0cec4dab5f920c5d7bdba"
        "c6aec0d4b4cfb4d92e0d0a");
    const std::optional<TextEncoding> guessed = litecode::core::guessTextEncoding(bytes);
    QVERIFY(guessed.has_value());
    QCOMPARE(litecode::core::textEncodingKey(*guessed), QStringLiteral("euckr"));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("12-euc-kr.txt"));
    QVERIFY(writeBytes(path, bytes));
    const DocumentLoadResult load =
        DocumentSession::loadBounded(path, std::nullopt, TextEncoding::Utf8, true);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(load.encoding, TextEncoding::EucKr);
    QCOMPARE(load.contents,
             QStringLiteral("EUC-KR\r\n안녕하세요. 한국어 인코딩 테스트입니다.\r\n").toUtf8());
}

void DocumentEncodingTest::handlesEmptyAndInvalidBomPayloads() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString emptyPath = directory.filePath(QStringLiteral("empty.txt"));
    QVERIFY(writeBytes(emptyPath, {}));
    const DocumentLoadResult empty = DocumentSession::loadBounded(emptyPath);
    QCOMPARE(empty.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(empty.encoding, TextEncoding::Utf8);
    QVERIFY(empty.contents.isEmpty());

    const QString bomOnlyPath = directory.filePath(QStringLiteral("bom-only.txt"));
    QVERIFY(writeBytes(bomOnlyPath, QByteArray::fromHex("EFBBBF")));
    const DocumentLoadResult bomOnly = DocumentSession::loadBounded(bomOnlyPath);
    QCOMPARE(bomOnly.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(bomOnly.encoding, TextEncoding::Utf8Bom);
    QVERIFY(bomOnly.contents.isEmpty());

    const QString invalidPath = directory.filePath(QStringLiteral("invalid-bom.txt"));
    QVERIFY(writeBytes(invalidPath, QByteArray::fromHex("EFBBBFFF")));
    const DocumentLoadResult invalid = DocumentSession::loadBounded(invalidPath);
    QCOMPARE(invalid.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(invalid.encoding, TextEncoding::Utf8Bom);
    QCOMPARE(QString::fromUtf8(invalid.contents), QString(QChar::ReplacementCharacter));
}

void DocumentEncodingTest::roundTripsNonLatinTextAndReplacesMalformedUtf16() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray originalText = QStringLiteral("漢字 😀\r\n").toUtf8();
    const QByteArray editedText = QStringLiteral("漢字 😀 edited\r\n").toUtf8();

    struct Case final {
        QString name;
        TextEncoding encoding;
        bool includeByteOrderMark;
    };
    const QVector<Case> cases{
        {QStringLiteral("utf8.txt"), TextEncoding::Utf8, false},
        {QStringLiteral("utf8bom.txt"), TextEncoding::Utf8Bom, true},
        {QStringLiteral("utf16le.txt"), TextEncoding::Utf16Le, true},
        {QStringLiteral("utf16be.txt"), TextEncoding::Utf16Be, true},
        {QStringLiteral("utf16le-nobom.txt"), TextEncoding::Utf16Le, false},
        {QStringLiteral("utf16be-nobom.txt"), TextEncoding::Utf16Be, false},
    };

    quint64 documentId = 20;
    for (const Case& item : cases) {
        QByteArray bytes;
        QVERIFY(litecode::core::encodeText(originalText, item.encoding, &bytes, nullptr,
                                           item.includeByteOrderMark));
        const QString path = directory.filePath(item.name);
        QVERIFY(writeBytes(path, bytes));
        const std::optional<TextEncoding> requested =
            item.includeByteOrderMark || item.encoding == TextEncoding::Utf8
                ? std::nullopt
                : std::optional{item.encoding};
        const DocumentLoadResult load = DocumentSession::loadBounded(path, requested);
        QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
        QCOMPARE(load.contents, originalText);
        QCOMPARE(load.hasByteOrderMark, item.includeByteOrderMark);

        DocumentSession session(DocumentId(documentId++), path);
        QVERIFY(session.acceptLoad(load));
        QString error;
        QVERIFY2(session.save(editedText, &error), qPrintable(error));
        const DocumentLoadResult reopened = DocumentSession::loadBounded(path, requested);
        QCOMPARE(reopened.outcome, DocumentLoadResult::Outcome::Loaded);
        QCOMPARE(reopened.contents, editedText);
        const bool expectsByteOrderMark = item.includeByteOrderMark ||
                                          item.encoding == TextEncoding::Utf16Le ||
                                          item.encoding == TextEncoding::Utf16Be;
        QCOMPARE(reopened.hasByteOrderMark, expectsByteOrderMark);
    }

    const QString truncatedPath = directory.filePath(QStringLiteral("truncated-utf16.txt"));
    QVERIFY(writeBytes(truncatedPath, QByteArray::fromHex("680069")));
    const DocumentLoadResult truncated =
        DocumentSession::loadBounded(truncatedPath, TextEncoding::Utf16Le);
    QCOMPARE(truncated.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(QString::fromUtf8(truncated.contents),
             QStringLiteral("h") + QChar::ReplacementCharacter);

    const QString surrogatePath = directory.filePath(QStringLiteral("unpaired-surrogate.txt"));
    QVERIFY(writeBytes(surrogatePath, QByteArray::fromHex("00D8")));
    const DocumentLoadResult surrogate =
        DocumentSession::loadBounded(surrogatePath, TextEncoding::Utf16Le);
    QCOMPARE(surrogate.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(QString::fromUtf8(surrogate.contents), QString(QChar::ReplacementCharacter));
}

void DocumentEncodingTest::preservesUtf8BomWhenExplicitlyDecoded() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("utf8-bom.txt"));
    QVERIFY(writeBytes(path, QByteArray::fromHex("EFBBBF68656C6C6F")));

    const DocumentLoadResult load = DocumentSession::loadBounded(path, TextEncoding::Utf8);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(load.encoding, TextEncoding::Utf8Bom);
    QVERIFY(load.hasByteOrderMark);

    DocumentSession session(DocumentId(30), path);
    QVERIFY(session.acceptLoad(load));
    QCOMPARE(session.preferredEncoding(), std::optional{TextEncoding::Utf8Bom});
    QString error;
    QVERIFY2(session.save(QByteArrayLiteral("edited"), &error), qPrintable(error));
    QCOMPARE(readBytes(path), QByteArray::fromHex("EFBBBF656469746564"));

    QVERIFY(!litecode::core::supportedTextDecodingEncodings().contains(TextEncoding::Utf8Bom));
    QVERIFY(litecode::core::supportedTextEncodings().contains(TextEncoding::Utf8Bom));
}

void DocumentEncodingTest::appliesUtf8BomDefaultOnNormalSave() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("default-utf8-bom.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("plain")));

    const DocumentLoadResult load =
        DocumentSession::loadBounded(path, std::nullopt, TextEncoding::Utf8Bom);
    QCOMPARE(load.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(load.encoding, TextEncoding::Utf8);
    QVERIFY(!load.hasByteOrderMark);

    DocumentSession session(DocumentId(33), path);
    QVERIFY(session.acceptLoad(load));
    QString error;
    QVERIFY2(session.save(QByteArrayLiteral("edited"), &error), qPrintable(error));
    QCOMPARE(readBytes(path), QByteArray::fromHex("EFBBBF656469746564"));
    QVERIFY(session.hasByteOrderMark());

    QVERIFY(writeBytes(path, QByteArrayLiteral("external")));
    const DocumentLoadResult reload =
        DocumentSession::loadBounded(path, session.preferredEncoding(), session.defaultEncoding());
    QCOMPARE(reload.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(reload.encoding, TextEncoding::Utf8);
    QVERIFY(!reload.hasByteOrderMark);
    QVERIFY(session.acceptReload(reload));
    QVERIFY2(session.save(QByteArrayLiteral("resaved"), &error), qPrintable(error));
    QCOMPARE(readBytes(path), QByteArray::fromHex("EFBBBF72657361766564"));

    const QString copyPath = directory.filePath(QStringLiteral("copy.txt"));
    QVERIFY2(session.saveAs(copyPath, QByteArrayLiteral("copied"), nullptr, &error),
             qPrintable(error));
    QCOMPARE(readBytes(copyPath), QByteArray::fromHex("EFBBBF636F70696564"));
}

void DocumentEncodingTest::honorsBomWhenReloadUsesPreviousEncodingAsFallback() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("externally-changed.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("plain utf-8")));

    const DocumentLoadResult initial = DocumentSession::loadBounded(path);
    QCOMPARE(initial.outcome, DocumentLoadResult::Outcome::Loaded);
    DocumentSession session(DocumentId(31), path);
    QVERIFY(session.acceptLoad(initial));
    QVERIFY(!session.preferredEncoding().has_value());

    QVERIFY(writeBytes(path, QByteArray::fromHex("FEFF006800E9000A")));
    const DocumentLoadResult reload =
        DocumentSession::loadBounded(path, session.preferredEncoding(), session.encoding());
    QCOMPARE(reload.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(reload.encoding, TextEncoding::Utf16Be);
    QVERIFY(reload.hasByteOrderMark);
    QCOMPARE(reload.contents, QStringLiteral("hé\n").toUtf8());
    QVERIFY(session.acceptReload(reload));
    QCOMPARE(session.encoding(), TextEncoding::Utf16Be);
    QVERIFY(!session.preferredEncoding().has_value());
}

void DocumentEncodingTest::preservesExplicitEncodingAcrossReload() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("explicit-encoding.txt"));
    QVERIFY(writeBytes(path, QByteArrayLiteral("plain utf-8")));

    const DocumentLoadResult automatic = DocumentSession::loadBounded(path);
    QCOMPARE(automatic.outcome, DocumentLoadResult::Outcome::Loaded);
    DocumentSession session(DocumentId(32), path);
    QVERIFY(session.acceptLoad(automatic));
    QVERIFY(!session.preferredEncoding().has_value());

    QVERIFY(writeBytes(path, QByteArray::fromHex("636166E9")));
    const DocumentLoadResult explicitReload =
        DocumentSession::loadBounded(path, TextEncoding::Windows1252);
    QCOMPARE(explicitReload.outcome, DocumentLoadResult::Outcome::Loaded);
    QVERIFY(session.acceptReload(explicitReload));
    QCOMPARE(session.preferredEncoding(), std::optional{TextEncoding::Windows1252});

    QVERIFY(writeBytes(path, QByteArray::fromHex("EFBBBF68656C6C6F")));
    const DocumentLoadResult reload =
        DocumentSession::loadBounded(path, session.preferredEncoding(), session.encoding());
    QCOMPARE(reload.outcome, DocumentLoadResult::Outcome::Loaded);
    QCOMPARE(reload.encoding, TextEncoding::Windows1252);
    QCOMPARE(reload.contents, QStringLiteral("ï»¿hello").toUtf8());
    QVERIFY(session.acceptReload(reload));
    QCOMPARE(session.preferredEncoding(), std::optional{TextEncoding::Windows1252});
}

QTEST_MAIN(DocumentEncodingTest)
#include "tst_document_encoding.moc"
