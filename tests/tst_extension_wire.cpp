#include "extensions/ExtensionWireProtocol.h"
#include "extensions/DialogWire.h"
#include "extensions/ProgressWire.h"
#include "extensions/WorkspaceEditWire.h"
#include "extensions/WorkspaceEditTransfer.h"
#include <QJsonArray>

#include <QJsonDocument>
#include <QtEndian>
#include <QtTest>

using namespace litecode::extensions::wire;

namespace {
QByteArray frame(const QByteArray& body) {
    QByteArray bytes(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), bytes.data());
    bytes += body;
    return bytes;
}
} // namespace

class ExtensionWireTests final : public QObject {
    Q_OBJECT
  private slots:
    void largeWorkspaceEditsCrossFramedTransportOnlyWhenComplete() {
        using namespace litecode::extensions;
        WorkspaceEdit edit;
        const QUrl uri("file:///workspace/large.txt");
        edit.insert(uri, Position(0, 0), QString(MaxMessageBytes + 1, QLatin1Char('x')) + QString::fromUtf8("\xf0\x9f\x98\x80"));
        auto request = encodeWorkspaceEdit({detail::WorkspaceEditAccess::entries(edit), {{uri.toString(QUrl::FullyEncoded), 3}}, {}});
        request.insert("event", "workspaceApplyEdit");
        request.insert("requestId", "17");
        QVERIFY(encode(request).isEmpty());
        QVector<QJsonObject> frames;
        sendWorkspaceEdit(request, [&](const auto& message) { frames.append(message); });
        QVERIFY(frames.size() > 3);
        QCOMPARE(frames.first().value("event").toString(), QString("workspaceEditBegin"));
        WorkspaceEditTransferDecoder editDecoder;
        Decoder envelopeDecoder;
        QString error;
        std::optional<QJsonObject> completed;
        for (qsizetype index = 0; index < frames.size(); ++index) {
            const auto encoded = encode(frames[index]);
            QVERIFY(!encoded.isEmpty());
            const auto decoded = envelopeDecoder.consume(encoded);
            QVERIFY(decoded.error.isEmpty());
            QCOMPARE(decoded.messages.size(), 1);
            completed = editDecoder.accept(decoded.messages.first(), &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(completed.has_value(), index == frames.size() - 1);
        }
        QCOMPARE(completed.value(), request);
        QVERIFY(!editDecoder.transferring());
        const auto restored = decodeWorkspaceEdit(*completed);
        QVERIFY(restored);
        QCOMPARE(std::get<detail::WorkspaceTextEntry>(restored->entries.first()).edit.newText, edit.get(uri).first().newText);
    }
    void smallWorkspaceEditsKeepSingleFramePath() {
        const QJsonObject request{{"event", "workspaceApplyEdit"}, {"requestId", "1"}, {"entries", QJsonArray{}}, {"versions", QJsonObject{}}};
        QVector<QJsonObject> sent;
        sendWorkspaceEdit(request, [&](const auto& frame) { sent.append(frame); });
        QCOMPARE(sent, QVector<QJsonObject>{request});
    }
    void workspaceEditTransfersRejectMalformedFramesAndReset() {
        const QJsonObject begin{{"event", "workspaceEditBegin"}, {"requestId", "1"}, {"length", "2"}};
        const QJsonObject chunk{{"event", "workspaceEditChunk"}, {"requestId", "1"}, {"sequence", "0"}, {"data", "e30="}};
        WorkspaceEditTransferDecoder decoder;
        QString error;
        const auto start = [&] {
            decoder.reset();
            decoder.accept(begin, &error);
        };
        start();
        QVERIFY(error.isEmpty());
        QVERIFY(decoder.transferring());
        auto invalid = chunk;
        invalid.insert("sequence", "1");
        QVERIFY(!decoder.accept(invalid, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!decoder.transferring());
        start();
        invalid = chunk;
        invalid.insert("requestId", "2");
        QVERIFY(!decoder.accept(invalid, &error));
        QVERIFY(!error.isEmpty());
        start();
        invalid = chunk;
        invalid.insert("data", "e30=\n");
        QVERIFY(!decoder.accept(invalid, &error));
        QVERIFY(!error.isEmpty());
        start();
        invalid = chunk;
        invalid.insert("data", QString::fromLatin1(QByteArray("long").toBase64()));
        QVERIFY(!decoder.accept(invalid, &error));
        QVERIFY(!error.isEmpty());
        start();
        QVERIFY(!decoder.accept({{"event", "workspaceEditEnd"}, {"requestId", "1"}, {"sequence", "0"}}, &error));
        QVERIFY(!error.isEmpty());
        start();
        QVERIFY(!decoder.accept(begin, &error));
        QVERIFY(!error.isEmpty());
        start();
        QVERIFY(!decoder.accept({{"event", "progress"}}, &error));
        QVERIFY(!error.isEmpty());
        invalid = begin;
        invalid.insert("length", QString::number(MaxWorkspaceEditTransferBytes + 1));
        QVERIFY(!decoder.accept(invalid, &error));
        QVERIFY(!error.isEmpty());
        invalid = begin;
        invalid.insert("length", "02");
        QVERIFY(!decoder.accept(invalid, &error));
        QVERIFY(!error.isEmpty());
        // Valid JSON is insufficient: a reconstructed envelope must retain its
        // request identity and must be an applyEdit, not another host event.
        start();
        QVERIFY(!decoder.accept(chunk, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!decoder.accept({{"event", "workspaceEditEnd"}, {"requestId", "1"}, {"sequence", "1"}}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!decoder.transferring());
        start();
        QVERIFY(!decoder.accept(chunk, nullptr));
        QVERIFY(!decoder.accept(chunk, nullptr));
        QVERIFY(!decoder.transferring());
    }
    void workspaceEditCodecPreservesOrderVersionsAndConfirmation() {
        using namespace litecode::extensions;
        WorkspaceEdit edit;
        const QUrl uri("file:///workspace/test.txt");
        edit.insert(uri, Position(0, 0), "first", WorkspaceEditEntryMetadata{true, "Review", QString("description"), {}});
        edit.createFile(QUrl("file:///workspace/new.txt"), {{}, {}, QByteArray("bytes")});
        edit.renameFile(uri, QUrl("file:///workspace/renamed.txt"), {true, false});
        const WorkspaceEditRequest request{detail::WorkspaceEditAccess::entries(edit), {{uri.toString(QUrl::FullyEncoded), 7}}, WorkspaceEditMetadata{true}};
        const auto encoded = encodeWorkspaceEdit(request);
        const auto decoded = decodeWorkspaceEdit(encoded);
        QVERIFY(decoded);
        QCOMPARE(decoded->entries.size(), 3);
        const auto& text = std::get<detail::WorkspaceTextEntry>(decoded->entries[0]);
        QCOMPARE(text.edit.newText, QString("first"));
        QVERIFY(text.metadata->needsConfirmation);
        QCOMPARE(text.metadata->description, std::optional(QString("description")));
        QCOMPARE(std::get<detail::WorkspaceCreateEntry>(decoded->entries[1]).options.contents, std::optional(QByteArray("bytes")));
        QCOMPARE(decoded->versions.value(uri.toString(QUrl::FullyEncoded)), qint64(7));
        auto malformed = encoded;
        malformed.insert("versions", QJsonObject{{uri.toString(QUrl::FullyEncoded), "07"}});
        QVERIFY(!decodeWorkspaceEdit(malformed));
    }
    void progressOptionsAndDeltaReports() {
        using namespace litecode::extensions;
        for (const auto location : {ProgressLocation::SourceControl, ProgressLocation::Window, ProgressLocation::Notification}) {
            const ProgressOptions options{location, QString("Working"), false};
            const auto encoded = encodeProgressOptions(options);
            const auto decoded = decodeProgressOptions(encoded);
            QVERIFY(decoded);
            QCOMPARE(std::get<ProgressLocation>(decoded->location), location);
            QVERIFY(decoded->cancellable.has_value());
            QVERIFY(!*decoded->cancellable);
            QCOMPARE(encodeProgressOptions(*decoded), encoded);
        }
        const ProgressOptions view{ProgressOptions::ViewLocation{"example.view"}, {}, {}};
        const auto decoded = decodeProgressOptions(encodeProgressOptions(view));
        QVERIFY(decoded);
        QCOMPARE(std::get<ProgressOptions::ViewLocation>(decoded->location).viewId, QString("example.view"));
        QVERIFY(!decoded->title);
        QVERIFY(!decoded->cancellable);
        QVERIFY(!decodeProgressOptions({}));
        QVERIFY(!decodeProgressOptions({{"location", 2}}));
        QVERIFY(!decodeProgressOptions({{"location", 15}, {"cancellable", "true"}}));
        const auto report = decodeProgressReport(encodeProgressReport({QString("Next"), 12.5}));
        QVERIFY(report);
        QCOMPARE(report->message.value(), QString("Next"));
        QCOMPARE(report->increment.value(), 12.5);
        QVERIFY(decodeProgressReport({}));
        QVERIFY(!decodeProgressReport({{"increment", true}}));
        QVERIFY(!decodeProgressReport({{"message", 42}}));
    }
    void fileDialogOptionsPreserveAbsenceAndOrder() {
        litecode::extensions::OpenDialogOptions open;
        QCOMPARE(encodeOpenDialogOptions(open), QJsonObject{});
        open.canSelectFiles = false;
        open.canSelectFolders = false;
        open.canSelectMany = true;
        open.defaultUri = QUrl::fromLocalFile("/some folder/file.txt");
        open.filters = QVector<std::pair<QString, QStringList>>{{"Z first", {"txt"}}, {"A second", {"png", "jpg"}}};
        const auto encoded = encodeOpenDialogOptions(open);
        const auto decoded = decodeOpenDialogOptions(encoded);
        QVERIFY(decoded);
        QVERIFY(decoded->canSelectFiles.has_value());
        QVERIFY(!*decoded->canSelectFiles);
        QCOMPARE(decoded->defaultUri.value(), open.defaultUri.value());
        QCOMPARE(decoded->filters->first().first, QString("Z first"));
        QCOMPARE(decoded->filters->last().second, QStringList({"png", "jpg"}));
        QCOMPARE(encodeOpenDialogOptions(*decoded), encoded);
        QVERIFY(!decodeOpenDialogOptions({{"canSelectFiles", 0}}));
        QVERIFY(!decodeOpenDialogOptions({{"defaultUri", true}}));
        QVERIFY(!decodeSaveDialogOptions({{"saveLabel", 1}}));
        QVERIFY(!decodeSaveDialogOptions({{"filters", QJsonArray{QJsonObject{{"name", "Bad"}, {"extensions", QJsonArray{42}}}}}}));
        litecode::extensions::SaveDialogOptions save;
        save.saveLabel = "Save it";
        save.title = "Save title";
        save.filters = open.filters;
        const auto saved = decodeSaveDialogOptions(encodeSaveDialogOptions(save));
        QVERIFY(saved);
        QCOMPARE(saved->saveLabel.value(), QString("Save it"));
        QCOMPARE(saved->filters->first().first, QString("Z first"));
    }
    void fragmentedAndCombinedFrames() {
        const QJsonObject first{{"event", "hello"}, {"text", "line one\nline two"}};
        const QJsonObject second{{"event", "ready"}};
        const auto bytes = encode(first) + encode(second);
        Decoder decoder;
        QVector<QJsonObject> received;
        for (char byte : bytes) {
            const auto result = decoder.consume(QByteArray(1, byte));
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            received += result.messages;
        }
        QCOMPARE(received, QVector<QJsonObject>({first, second}));

        decoder.reset();
        const auto combined = decoder.consume(bytes);
        QVERIFY(combined.error.isEmpty());
        QCOMPARE(combined.messages, received);
    }

    void rejectsInvalidLengthAndStaysFailed() {
        Decoder decoder;
        auto zero = decoder.consume(QByteArray(4, '\0'));
        QVERIFY(!zero.error.isEmpty());
        QVERIFY(!decoder.consume(encode({})).error.isEmpty());
        decoder.reset();
        QByteArray huge(4, '\0');
        qToBigEndian<quint32>(MaxMessageBytes + 1, huge.data());
        QVERIFY(!decoder.consume(huge).error.isEmpty());
    }

    void rejectsMalformedAndUnsupportedEnvelopes() {
        Decoder decoder;
        QVERIFY(!decoder.consume(frame("not json")).error.isEmpty());
        decoder.reset();
        QVERIFY(!decoder.consume(frame(QJsonDocument(QJsonObject{
            {"protocol", ProtocolVersion + 1}, {"message", QJsonObject{}}})
                                           .toJson(QJsonDocument::Compact))).error.isEmpty());
        decoder.reset();
        QVERIFY(!decoder.consume(frame(QJsonDocument(QJsonObject{
            {"protocol", ProtocolVersion}, {"message", "wrong type"}})
                                           .toJson(QJsonDocument::Compact))).error.isEmpty());
    }

    void boundsOutboundMessages() {
        QVERIFY(!encode(QJsonObject{{"text", "small"}}).isEmpty());
        QVERIFY(encode(QJsonObject{{"text", QString(MaxMessageBytes, 'x')}}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ExtensionWireTests)
#include "tst_extension_wire.moc"
