#include "extensions/HostTextEditorEdit.h"
#include <QtTest>
#include <atomic>
#include <thread>

using namespace litecode::extensions;
namespace {
std::shared_ptr<HostTextDocument> document() {
    return std::make_shared<HostTextDocument>(TextDocumentSnapshot{"edit-document",
                                                                   QUrl("untitled:editor-edit"),
                                                                   "plaintext",
                                                                   "utf8",
                                                                   "first\nsecond\n",
                                                                   7,
                                                                   false,
                                                                   EndOfLine::LF,
                                                                   {}},
                                              nullptr);
}
} // namespace
class EditorEditTests final : public QObject {
    Q_OBJECT
  private slots:
    void capturesVersionAndUndoStopsWithoutMutatingDocument() {
        const auto source = document();
        const auto original = source->getText();
        const auto edits = HostTextEditorEdit::collect(
            source,
            [&](auto edit) {
                edit->replace(Range(0, 0, 0, 2), "replacement\r\n");
                edit->insert(Position(1, 0), "insert");
                edit->delete_(Selection(1, 6, 1, 3));
                edit->setEndOfLine(EndOfLine::CRLF);
            },
            false, true);
        QCOMPARE(edits.documentVersionId, qint64(7));
        QVERIFY(!edits.undoStopBefore);
        QVERIFY(edits.undoStopAfter);
        QCOMPARE(edits.setEndOfLine, std::optional(EndOfLine::CRLF));
        QCOMPARE(edits.edits.size(), 3);
        QCOMPARE(edits.edits[0].text, std::optional(QString("replacement\r\n")));
        QVERIFY(!edits.edits[0].forceMoveMarkers);
        QVERIFY(edits.edits[1].forceMoveMarkers);
        QVERIFY(!edits.edits[2].text);
        QVERIFY(edits.edits[2].forceMoveMarkers);
        QVERIFY(edits.edits[2].range.isEqual(Range(1, 3, 1, 6)));
        QCOMPARE(source->version(), qint64(7));
        QCOMPARE(source->getText(), original);
        QCOMPARE(source->eol(), EndOfLine::LF);
    }
    void clampsRangesAndPreservesCoincidentInsertionOrder() {
        const auto edits = HostTextEditorEdit::collect(document(), [](auto edit) {
            edit->replace(Position(100, 100), "first");
            edit->insert(Position(100, 100), "second");
            edit->replace(Range(1, 100, 2, 100), "last");
        });
        QCOMPARE(edits.edits.size(), 3);
        QVERIFY(edits.edits[0].range.isEqual(Range(2, 0, 2, 0)));
        QVERIFY(edits.edits[1].range.isEqual(Range(2, 0, 2, 0)));
        QVERIFY(edits.edits[2].range.isEqual(Range(1, 6, 2, 0)));
        QCOMPARE(edits.edits[0].text, std::optional(QString("first")));
        QCOMPARE(edits.edits[1].text, std::optional(QString("second")));
        QCOMPARE(edits.edits[2].text, std::optional(QString("last")));
        QVERIFY(edits.undoStopBefore && edits.undoStopAfter);
    }
    void allowsTouchingRangesButRejectsOverlapAfterClamping() {
        const auto touching = HostTextEditorEdit::collect(document(), [](auto edit) {
            edit->replace(Range(0, 2, 0, 4), "later");
            edit->replace(Range(0, 0, 0, 2), "earlier");
            edit->insert(Position(0, 2), "boundary");
        });
        QCOMPARE(touching.edits.size(), 3);
        // Validation uses a separate range list and must not sort this payload.
        QCOMPARE(touching.edits[0].text, std::optional(QString("later")));
        QVERIFY_EXCEPTION_THROWN(HostTextEditorEdit::collect(document(),
                                                             [](auto edit) {
                                                                 edit->replace(Range(0, 0, 0, 4),
                                                                               "a");
                                                                 edit->insert(Position(0, 2), "b");
                                                             }),
                                 std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(HostTextEditorEdit::collect(document(),
                                                             [](auto edit) {
                                                                 edit->replace(Range(0, 0, 0, 100),
                                                                               "a");
                                                                 edit->delete_(Range(0, 4, 1, 0));
                                                             }),
                                 std::invalid_argument);
    }
    void rejectsEveryMutationAfterCallbackAndOnCallbackFailure() {
        std::shared_ptr<TextEditorEdit> retained;
        HostTextEditorEdit::collect(document(), [&](auto edit) { retained = std::move(edit); });
        QVERIFY_EXCEPTION_THROWN(retained->replace(Position(0, 0), "x"), std::logic_error);
        QVERIFY_EXCEPTION_THROWN(retained->replace(Range(0, 0, 0, 1), "x"), std::logic_error);
        QVERIFY_EXCEPTION_THROWN(retained->insert(Position(0, 0), "x"), std::logic_error);
        QVERIFY_EXCEPTION_THROWN(retained->delete_(Range(0, 0, 0, 1)), std::logic_error);
        QVERIFY_EXCEPTION_THROWN(retained->setEndOfLine(EndOfLine::LF), std::logic_error);
        QVERIFY_EXCEPTION_THROWN(
            HostTextEditorEdit::collect(document(),
                                        [&](auto edit) {
                                            retained = std::move(edit);
                                            retained->insert(Position(0, 0), "not committed");
                                            throw std::runtime_error("Callback failure");
                                        }),
            std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(retained->insert(Position(0, 0), "x"), std::logic_error);
        QVERIFY_EXCEPTION_THROWN(
            HostTextEditorEdit::collect(document(),
                                        [&](auto edit) {
                                            retained = std::move(edit);
                                            retained->replace(Range(0, 0, 0, 4), "a");
                                            retained->replace(Range(0, 1, 0, 2), "b");
                                        }),
            std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(retained->insert(Position(0, 0), "x"), std::logic_error);
    }
    void retainedBuilderDoesNotKeepDocumentAlive() {
        auto source = document();
        const std::weak_ptr<HostTextDocument> weak = source;
        std::shared_ptr<TextEditorEdit> retained;
        HostTextEditorEdit::collect(source, [&](auto edit) { retained = std::move(edit); });
        source.reset();
        QVERIFY(weak.expired());
        QVERIFY_EXCEPTION_THROWN(retained->setEndOfLine(EndOfLine::LF), std::logic_error);
    }
    void refusesConcurrentMutationButDoesNotInvalidateTheCallback() {
        std::atomic_bool refused{};
        const auto edits = HostTextEditorEdit::collect(document(), [&](auto edit) {
            std::thread worker([&, edit] {
                try {
                    edit->insert(Position(0, 0), "worker");
                } catch (const std::logic_error&) {
                    refused = true;
                }
            });
            worker.join();
            edit->insert(Position(0, 0), "callback");
        });
        QVERIFY(refused.load());
        QCOMPARE(edits.edits.size(), 1);
        QCOMPARE(edits.edits[0].text, std::optional(QString("callback")));
    }
    void emptyReplacementIsNotDeletionAndLastEolWins() {
        const auto edits = HostTextEditorEdit::collect(document(), [](auto edit) {
            edit->replace(Range(0, 0, 0, 1), QString{});
            edit->delete_(Range(0, 1, 0, 2));
            edit->setEndOfLine(EndOfLine::CRLF);
            edit->setEndOfLine(EndOfLine::LF);
            QVERIFY_EXCEPTION_THROWN(edit->setEndOfLine(static_cast<EndOfLine>(3)),
                                     std::invalid_argument);
        });
        QVERIFY(edits.edits[0].text.has_value());
        QVERIFY(edits.edits[0].text->isEmpty());
        QVERIFY(!edits.edits[1].text.has_value());
        QCOMPARE(edits.setEndOfLine, std::optional(EndOfLine::LF));
        QVERIFY(!edits.empty());
        QVERIFY(HostTextEditorEdit::collect(document(), [](auto) {}).empty());
        QVERIFY(!HostTextEditorEdit::collect(document(), [](auto edit) {
                     edit->setEndOfLine(EndOfLine::CRLF);
                 }).empty());
    }
    void invalidInputsNeverCallCallback() {
        bool called = false;
        QVERIFY_EXCEPTION_THROWN(HostTextEditorEdit::collect(nullptr, [&](auto) { called = true; }),
                                 std::invalid_argument);
        QVERIFY(!called);
        QVERIFY_EXCEPTION_THROWN(HostTextEditorEdit::collect(document(), {}),
                                 std::invalid_argument);
    }
};
QTEST_GUILESS_MAIN(EditorEditTests)
#include "tst_extension_editor_edit.moc"
