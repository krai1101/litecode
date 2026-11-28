#include "extensions/ChatHistory.h"
#include "extensions/ChatHistoryWire.h"
#include "extensions/ChatRequestWire.h"
#include "extensions/ChatStreamLedger.h"
#include <QJsonArray>
#include <QtTest>

using litecode::extensions::ChatHistory;
using litecode::extensions::ChatStreamLedger;

class ChatStreamLedgerTests final : public QObject {
    Q_OBJECT
  private slots:
    void requestIdentityAndOrderedParts() {
        ChatStreamLedger ledger;
        QVERIFY(!ledger.start(0));
        QVERIFY(ledger.start(41));
        QVERIFY(!ledger.start(41));
        QVERIFY(ledger.start(42));
        const QVector<QJsonObject> parts{
            {{"kind", "markdown"}, {"value", "test"}},
            {{"kind", "anchor"}, {"value", "file:///example.py"}, {"title", "Example"}},
            {{"kind", "button"},
             {"value", QJsonObject{{"command", "test.open"}, {"title", "Open"}}}},
            {{"kind", "filetree"},
             {"value", QJsonArray{QJsonObject{{"name", "file.py"}}}},
             {"baseUri", "file:///workspace"}},
            {{"kind", "progress"}, {"value", "Working"}},
            {{"kind", "reference"}, {"value", "file:///example.py"}}};
        quint64 sequence = 0;
        for (const auto& payload : parts) {
            const auto part = ledger.append(41, payload);
            QVERIFY(part.has_value());
            QCOMPARE(part->sequence, sequence++);
        }
        const auto other = ledger.append(42, {{"kind", "markdown"}, {"value", "other"}});
        QVERIFY(other.has_value());
        QCOMPARE(other->sequence, quint64(0));
        QVERIFY(ledger.finish(41));
        QVERIFY(!ledger.finish(41));
        QVERIFY(!ledger.append(41, {{"kind", "markdown"}, {"value", "late"}}));
    }
    void cancellationAndInvalidParts() {
        ChatStreamLedger ledger;
        QVERIFY(ledger.start(9));
        QVERIFY(!ledger.append(9, {{"kind", "unknown"}, {"value", "x"}}));
        QVERIFY(!ledger.append(9, {{"kind", "markdown"}}));
        QVERIFY(!ledger.append(9, {{"kind", "anchor"}, {"value", 12}}));
        QVERIFY(!ledger.append(9, {{"kind", "button"}, {"value", "not a command"}}));
        QVERIFY(!ledger.append(9, {{"kind", "filetree"}, {"value", QJsonArray{}}}));
        QVERIFY(!ledger.append(
            9, {{"kind", "filetree"},
                {"value", QJsonArray{}},
                {"baseUri", QJsonObject{{"uri", "file:///workspace"}, {"range", QJsonObject{}}}}}));
        QVERIFY(
            !ledger.append(9, {{"kind", "markdown"},
                               {"value", QJsonObject{{"value", "text"}, {"isTrusted", true}}}}));
        QVERIFY(!ledger.append(
            9, {{"kind", "markdown"},
                {"value",
                 QJsonObject{{"value", "text"},
                             {"isTrusted", QJsonObject{{"enabledCommands", QJsonArray{7}}}}}}}));
        QVERIFY(!ledger.append(9, {{"kind", "reference"},
                                   {"value", "file:///test.cpp"},
                                   {"iconPath", QJsonObject{{"id", 7}}}}));
        QVERIFY(!ledger.append(9, {{"kind", "button"},
                                   {"value", QJsonObject{{"command", "test.open"},
                                                         {"title", "Open"},
                                                         {"arguments", "wrong"}}}}));
        QVERIFY(!ledger.append(
            9,
            {{"kind", "anchor"},
             {"value",
              QJsonObject{
                  {"uri", "file:///test.cpp"},
                  {"range", QJsonObject{{"start", QJsonObject{{"line", 3}, {"character", 0}}},
                                        {"end", QJsonObject{{"line", 2}, {"character", 0}}}}}}}}));
        const auto first = ledger.append(9, {{"kind", "markdown"}, {"value", "ok"}});
        QVERIFY(first.has_value());
        QCOMPARE(first->sequence, quint64(0));
        QVERIFY(ledger.cancel(9));
        QVERIFY(ledger.isCancelled(9));
        QVERIFY(!ledger.cancel(9));
        QVERIFY(!ledger.append(9, {{"kind", "markdown"}, {"value", "late"}}));
        QVERIFY(ledger.finish(9));
        QVERIFY(ledger.start(9));
        QVERIFY(!ledger.isCancelled(9));
        ledger.clear();
        QVERIFY(!ledger.contains(9));
    }
    void participantHistoryKeepsOnlyContentParts() {
        ChatHistory history;
        const QJsonObject request{
            {"prompt", "Explain #file"},
            {"command", "explain"},
            {"references", QJsonArray{QJsonObject{{"id", "file"}, {"value", "test.cpp"}}}},
            {"toolReferences", QJsonArray{}}};
        const QJsonArray parts{
            QJsonObject{{"kind", "progress"}, {"value", "Reading"}},
            QJsonObject{{"kind", "markdown"}, {"value", "Answer"}},
            QJsonObject{{"kind", "reference"}, {"value", "file:///test.cpp"}},
            QJsonObject{{"kind", "anchor"}, {"value", "file:///test.cpp"}},
            QJsonObject{{"kind", "button"},
                        {"value", QJsonObject{{"command", "open"}, {"title", "Open"}}}},
            QJsonObject{
                {"kind", "filetree"}, {"value", QJsonArray{}}, {"baseUri", "file:///workspace"}}};
        const QJsonObject result{{"metadata", QJsonObject{{"model", "test"}}}};
        QVERIFY(history.record("tests.chat.one", request, parts, result));
        QVERIFY(history.history("tests.chat.other").isEmpty());
        const auto turns = history.history("tests.chat.one");
        QCOMPARE(turns.size(), 2);
        QCOMPARE(turns.at(0).toObject().value("kind").toString(), QString("request"));
        QCOMPARE(turns.at(0).toObject().value("prompt").toString(), QString("Explain #file"));
        QCOMPARE(turns.at(0).toObject().value("participant").toString(), QString("tests.chat.one"));
        QCOMPARE(turns.at(0).toObject().value("references").toArray().size(), 1);
        QCOMPARE(turns.at(1).toObject().value("kind").toString(), QString("response"));
        QCOMPARE(turns.at(1).toObject().value("response").toArray().size(), 4);
        QCOMPARE(turns.at(1).toObject().value("result").toObject(), result);
        QCOMPARE(turns.at(1).toObject().value("command").toString(), QString("explain"));
        QVERIFY(history.record("tests.chat.other", request, {}, {}));
        QCOMPARE(history.history("tests.chat.one").size(), 2);
        QCOMPARE(history.history("tests.chat.other").size(), 2);
        history.clear();
        QVERIFY(history.history("tests.chat.one").isEmpty());
    }
    void invalidHistoryDoesNotPartiallyAppend() {
        ChatHistory history;
        const QJsonObject request{
            {"prompt", "hello"}, {"references", QJsonArray{}}, {"toolReferences", QJsonArray{}}};
        QVERIFY(!history.record("", request, {}, {}));
        QVERIFY(!history.record("tests.chat", QJsonObject{{"prompt", "hello"}}, {}, {}));
        QVERIFY(!history.record("tests.chat", request, QJsonArray{QJsonObject{{"kind", "unknown"}}},
                                {}));
        QVERIFY(!history.record("tests.chat", request,
                                QJsonArray{QJsonObject{{"kind", "markdown"}}}, {}));
        QVERIFY(history.history("tests.chat").isEmpty());
        QVERIFY(history.record("tests.chat", request, {}, {}));
        QCOMPARE(history.history("tests.chat").size(), 2);
    }
    void requestReferencesAreTypedValidatedAndReverseOrdered() {
        const QJsonObject sourceRange{{"start", QJsonObject{{"line", 2}, {"character", 1}}},
                                      {"end", QJsonObject{{"line", 2}, {"character", 4}}}};
        const QJsonObject locationValue{{"uri", "file:///b.py"}, {"range", sourceRange}};
        const QJsonArray references{
            QJsonObject{{"id", "a"},
                        {"range", QJsonArray{0, 2}},
                        {"modelDescription", "Source"},
                        {"$litecodeValueType", "uri"},
                        {"value", "file:///a.py"}},
            QJsonObject{{"id", "attachment"}, {"value", QJsonObject{{"data", 7}}}},
            QJsonObject{{"id", "b"},
                        {"range", QJsonArray{3, 5}},
                        {"$litecodeValueType", "location"},
                        {"value", locationValue}}};
        const QJsonArray toolReferences{
            QJsonObject{{"name", "first"}, {"range", QJsonArray{0, 2}}},
            QJsonObject{{"name", "second"}, {"range", QJsonArray{3, 5}}}};
        const QJsonObject input{
            {"prompt", "#a #b"}, {"references", references}, {"toolReferences", toolReferences}};
        const auto normalized = litecode::extensions::wire::normalizeChatRequest(input);
        QVERIFY(normalized.has_value());
        const auto normalizedReferences = normalized->value("references").toArray();
        QCOMPARE(normalizedReferences.at(0).toObject().value("id").toString(), QString("b"));
        QCOMPARE(normalizedReferences.at(1).toObject().value("id").toString(), QString("a"));
        QCOMPARE(normalizedReferences.at(2).toObject().value("id").toString(),
                 QString("attachment"));
        const auto request = litecode::extensions::wire::decodeChatRequest(*normalized);
        QVERIFY(request.has_value());
        QCOMPARE(request->references.size(), 3);
        QCOMPARE(request->references.at(0).range->first, 3);
        QCOMPARE(request->references.at(1).modelDescription.value(), QString("Source"));
        QVERIFY(std::holds_alternative<litecode::extensions::Location>(
            request->references.at(0).value));
        const auto location =
            std::get<litecode::extensions::Location>(request->references.at(0).value);
        QCOMPARE(location.uri.toString(), QString("file:///b.py"));
        QCOMPARE(location.range.end().character(), 4);
        QVERIFY(std::holds_alternative<QUrl>(request->references.at(1).value));
        QVERIFY(std::holds_alternative<QJsonValue>(request->references.at(2).value));
        QCOMPARE(request->toolReferences.at(0).name, QString("second"));
        QCOMPARE(request->toolReferences.at(1).range->first, 0);
        QJsonObject invalid = input;
        invalid.insert("references", QJsonArray{QJsonObject{{"id", "missing"}}});
        QVERIFY(!litecode::extensions::wire::normalizeChatRequest(invalid));
        invalid.insert("references",
                       QJsonArray{QJsonObject{
                           {"id", "outside"}, {"range", QJsonArray{0, 6}}, {"value", "x"}}});
        QVERIFY(!litecode::extensions::wire::normalizeChatRequest(invalid));
        invalid.insert("references",
                       QJsonArray{QJsonObject{
                           {"id", "bad-uri"}, {"$litecodeValueType", "uri"}, {"value", ""}}});
        QVERIFY(!litecode::extensions::wire::normalizeChatRequest(invalid));
        invalid = input;
        invalid.insert("toolReferences", QJsonArray{QJsonObject{{"range", QJsonArray{0, 2}}}});
        QVERIFY(!litecode::extensions::wire::normalizeChatRequest(invalid));
        // QString/JavaScript prompt offsets count UTF-16 code units, not code points.
        invalid = QJsonObject{
            {"prompt", QString::fromUtf8("😀#a")},
            {"references",
             QJsonArray{QJsonObject{{"id", "a"}, {"range", QJsonArray{2, 4}}, {"value", "a"}}}}};
        QVERIFY(litecode::extensions::wire::normalizeChatRequest(invalid));
    }
    void historyDecoderRejectsInvalidTurnSequences() {
        ChatHistory history;
        const QJsonObject request{{"prompt", "hello"},
                                  {"command", "explain"},
                                  {"references", QJsonArray{}},
                                  {"toolReferences", QJsonArray{}}};
        const QJsonArray parts{
            QJsonObject{{"kind", "progress"}, {"value", "Working"}},
            QJsonObject{{"kind", "markdown"}, {"value", QJsonObject{{"value", "Answer"}}}},
            QJsonObject{{"kind", "reference"}, {"value", "file:///a.py"}},
            QJsonObject{{"kind", "button"},
                        {"value", QJsonObject{{"command", "open"}, {"title", "Open"}}}}};
        const QJsonObject result{{"metadata", QJsonObject{{"model", "test"}}}};
        QVERIFY(history.record("tests.chat", request, parts, result));
        const auto turns = history.history("tests.chat");
        const auto decoded = litecode::extensions::wire::decodeChatContext(turns, "tests.chat");
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->history.size(), 2);
        QVERIFY(
            std::holds_alternative<litecode::extensions::ChatRequestTurn>(decoded->history.at(0)));
        const auto& response =
            std::get<litecode::extensions::ChatResponseTurn>(decoded->history.at(1));
        QCOMPARE(response.response.size(), 2);
        QVERIFY(std::holds_alternative<litecode::extensions::ChatResponseMarkdownPart>(
            response.response.at(0)));
        QCOMPARE(response.result.metadata.value("model").toString(), QString("test"));
        QCOMPARE(response.command.value(), QString("explain"));
        QVERIFY(!litecode::extensions::wire::decodeChatContext(turns, "other.chat"));
        QVERIFY(!litecode::extensions::wire::decodeChatContext(QJsonArray{turns.first()},
                                                               "tests.chat"));
        QJsonArray invalid = turns;
        auto responseTurn = invalid.at(1).toObject();
        responseTurn.insert(
            "response", QJsonArray{QJsonObject{{"kind", "progress"}, {"value", "not content"}}});
        invalid.replace(1, responseTurn);
        QVERIFY(!litecode::extensions::wire::decodeChatContext(invalid, "tests.chat"));
        responseTurn = turns.at(1).toObject();
        responseTurn.insert("result", QJsonObject{{"metadata", "not an object"}});
        invalid.replace(1, responseTurn);
        QVERIFY(!litecode::extensions::wire::decodeChatContext(invalid, "tests.chat"));
        responseTurn = turns.at(1).toObject();
        responseTurn.insert("command", "different");
        invalid.replace(1, responseTurn);
        QVERIFY(!litecode::extensions::wire::decodeChatContext(invalid, "tests.chat"));
    }
};

QTEST_GUILESS_MAIN(ChatStreamLedgerTests)
#include "tst_chat_stream_ledger.moc"
