#include "extensions/ExtensionApi.h"
#include "extensions/WorkspaceEditApi.h"
#include "extensions/SnippetApi.h"
#include <QtTest>
#include <stdexcept>

using namespace litecode::extensions;

class ExtensionPrimitiveTests final : public QObject {
    Q_OBJECT
  private slots:
    void nestedEventDeliveryFinishesOlderListenersBeforeNewEvent() {
        EventEmitter<int> emitter;
        QStringList calls;
        auto first = emitter.event()([&](int value) {
            calls.append("first:" + QString::number(value));
            if (value == 1) {
                emitter.fire(2);
                calls.append("nested:return");
            }
        });
        auto second = emitter.event()([&](int value) { calls.append("second:" + QString::number(value)); });
        emitter.fire(1);
        QCOMPARE(calls, QStringList({"first:1", "second:1", "first:2", "second:2", "nested:return"}));
        first.dispose();
        second.dispose();
    }
    void eventDispatchRetainsEmitterStateAndPayloadAcrossOwnerDestruction() {
        auto emitter = std::make_unique<EventEmitter<QString>>();
        auto data = std::make_unique<QString>("owned payload");
        int skipped = 0;
        QString delivered;
        auto first = emitter->event()([&](const QString& value) {
            data.reset();
            emitter.reset();
            delivered = value;
        });
        auto second = emitter->event()([&](const QString&) { ++skipped; });
        emitter->fire(*data);
        QCOMPARE(delivered, QString("owned payload"));
        QCOMPARE(skipped, 0);
        first.dispose();
        second.dispose();
    }
    void disposableCopiesShareOneCleanupAndSupportDisposableLikes() {
        int calls = 0;
        Disposable first([&] { ++calls; });
        auto copy = first;
        copy.dispose();
        QVERIFY(!first);
        first.dispose();
        QCOMPARE(calls, 1);
        struct Like {
            int* calls;
            void dispose() { ++*calls; throw std::runtime_error("fixture cleanup"); }
        };
        Disposable second([&] { ++calls; });
        auto combined = Disposable::from(Like{&calls}, second);
        combined.dispose();
        combined.dispose();
        second.dispose();
        QCOMPARE(calls, 3);
        auto empty = Disposable::from();
        empty.dispose();
    }
    void eventSubscriptionsTrackTheSameHandleAndBindReceiver() {
        EventEmitter<int> emitter;
        std::vector<Disposable> subscriptions;
        struct Receiver {
            int sum{};
            void add(const int& value) { sum += value; }
        } receiver;
        auto first = emitter.event()(&Receiver::add, &receiver, &subscriptions);
        QCOMPARE(subscriptions.size(), size_t(1));
        emitter.fire(2);
        QCOMPARE(receiver.sum, 2);
        first.dispose();
        QVERIFY(!subscriptions.front());
        emitter.fire(4);
        QCOMPARE(receiver.sum, 2);
        auto second = emitter.event()([&](int value) { receiver.sum += value; }, &subscriptions);
        detail::disposeSubscriptions(subscriptions);
        QVERIFY(!second);
        emitter.fire(8);
        QCOMPARE(receiver.sum, 2);
    }
    void sourceDisposedBeforeTokenAccessStaysUncancelled() {
        CancellationTokenSource source;
        source.dispose();
        source.cancel();
        auto token = source.token();
        QVERIFY(!token.isCancellationRequested());
        source.cancel();
        QVERIFY(!token.isCancellationRequested());
        auto listener = token.onCancellationRequested([] {});
        QVERIFY(!listener);
    }
    void materializedTokenCanResubscribeAfterSourceDisposal() {
        CancellationTokenSource source;
        auto token = source.token();
        int oldCalls = 0, newCalls = 0;
        auto old = token.onCancellationRequested([&] { ++oldCalls; });
        source.dispose();
        std::vector<Disposable> subscriptions;
        auto fresh = token.onCancellationRequested([&] { ++newCalls; }, &subscriptions);
        source.cancel();
        QVERIFY(token.isCancellationRequested());
        QCOMPARE(oldCalls, 0);
        QCOMPARE(newCalls, 1);
        QCOMPARE(subscriptions.size(), size_t(1));
        detail::disposeSubscriptions(subscriptions);
        QVERIFY(!fresh);
        old.dispose();
    }
    void cancellationCanDestroyItsSourceDuringDelivery() {
        auto source = std::make_unique<CancellationTokenSource>();
        const auto token = source->token();
        int first = 0, skipped = 0;
        auto one = token.onCancellationRequested([&] { ++first; source.reset(); });
        auto two = token.onCancellationRequested([&] { ++skipped; });
        source->cancel();
        QVERIFY(!source);
        QVERIFY(token.isCancellationRequested());
        QCOMPARE(first, 1);
        QCOMPARE(skipped, 0);
        one.dispose();
        two.dispose();
    }
    void subscriptionCleanupIsOrderedAndReentrant() {
        std::vector<Disposable> subscriptions;
        QStringList calls;
        subscriptions.emplace_back([&] {
            calls.append("first");
            subscriptions.clear();
            subscriptions.emplace_back([&] { calls.append("nested"); });
            detail::disposeSubscriptions(subscriptions);
            throw std::runtime_error("cleanup failed");
        });
        subscriptions.emplace_back([&] { calls.append("second"); });
        detail::disposeSubscriptions(subscriptions);
        QCOMPARE(calls, QStringList({"first", "nested", "second"}));
        QVERIFY(subscriptions.empty());
        detail::disposeSubscriptions(subscriptions);
        QCOMPARE(calls.size(), 3);
    }
    void subscriptionCleanupDrainsAppendedHandles() {
        std::vector<Disposable> subscriptions;
        QStringList calls;
        subscriptions.emplace_back([&] {
            calls.append("first");
            subscriptions.emplace_back([&] { calls.append("appended"); });
        });
        subscriptions.emplace_back([&] { calls.append("second"); });
        detail::disposeSubscriptions(subscriptions);
        QCOMPARE(calls, QStringList({"first", "second", "appended"}));
        QVERIFY(subscriptions.empty());
    }
    void eventCaptureDestructionCanReenterOnRemovalAndShutdown() {
        struct Capture {
            std::function<void()> cleanup;
            ~Capture() { cleanup(); }
        };
        EventEmitter<int> emitter;
        int destroyed = 0;
        Disposable replacement;
        auto capture = std::make_shared<Capture>();
        capture->cleanup = [&] {
            ++destroyed;
            replacement = emitter.event()([](int) {});
        };
        auto listener = emitter.event()([capture](int) {});
        capture.reset();
        listener.dispose();
        QCOMPARE(destroyed, 1);
        QVERIFY(replacement);
        replacement.dispose();

        auto shutdownCapture = std::make_shared<Capture>();
        bool registrationRejected = false;
        shutdownCapture->cleanup = [&] {
            ++destroyed;
            registrationRejected = !emitter.event()([](int) {});
            emitter.dispose();
        };
        auto shutdownListener = emitter.event()([shutdownCapture](int) {});
        shutdownCapture.reset();
        emitter.dispose();
        QCOMPARE(destroyed, 2);
        QVERIFY(registrationRejected);
        shutdownListener.dispose();
    }
    void snippetBuildersEscapeEachContextAndReturnSelf() {
        SnippetString text;
        QVERIFY(&text.appendText(QStringLiteral("$}\\{")) == &text);
        QCOMPARE(text.value, QStringLiteral("\\$\\}\\\\{"));
        SnippetString placeholder;
        placeholder.appendPlaceholder(QStringLiteral("$}\\{"));
        QCOMPARE(placeholder.value, QStringLiteral("${1:\\$\\}\\\\{}"));
        SnippetString choice;
        choice.appendChoice({QStringLiteral("a,b"), QStringLiteral("x|y"), QStringLiteral("\\$}")});
        QCOMPARE(choice.value, QStringLiteral("${1|a\\,b,x\\|y,\\\\$}|}"));
        SnippetString variable;
        variable.appendVariable("NAME", QStringLiteral("\\$}"));
        QCOMPARE(variable.value, QStringLiteral("${NAME:\\\\$\\}}"));
        variable.appendVariable("EMPTY", QString{});
        QCOMPARE(variable.value, QStringLiteral("${NAME:\\\\$\\}}${EMPTY}"));
    }
    void snippetNestedBuildersShareOnlyAutomaticNumbering() {
        SnippetString snippet;
        snippet.appendTabstop(9).appendPlaceholder([](SnippetString& nested) {
            nested.appendTabstop().appendPlaceholder("inner");
        }).appendTabstop(0).appendChoice({"a", "b"});
        QCOMPARE(snippet.value, QStringLiteral("$9${1:$2${3:inner}}$0${4|a,b|}"));
        snippet.appendVariable("VAR", [](SnippetString& nested) { nested.appendTabstop(); }).appendTabstop();
        QCOMPARE(snippet.value, QStringLiteral("$9${1:$2${3:inner}}$0${4|a,b|}${VAR:$5}$6"));
        SnippetString explicitPlaceholder;
        explicitPlaceholder.appendPlaceholder([](SnippetString& nested) { nested.appendTabstop(); }, 7).appendTabstop();
        QCOMPARE(explicitPlaceholder.value, QStringLiteral("${7:$1}$2"));
    }
    void snippetBuilderFailureConsumesOuterButNotNestedSequence() {
        SnippetString snippet("prefix");
        QVERIFY_EXCEPTION_THROWN(snippet.appendPlaceholder([](SnippetString& nested) {
            nested.appendTabstop();
            throw std::runtime_error("builder failed");
        }), std::runtime_error);
        QCOMPARE(snippet.value, QStringLiteral("prefix"));
        snippet.appendTabstop();
        QCOMPARE(snippet.value, QStringLiteral("prefix$2"));
    }
    void snippetRawValuesAndEmptyDefaultsDoNotConsumeTabstops() {
        SnippetString snippet(QStringLiteral("${99:raw}"));
        snippet.appendVariable("VAR", [](SnippetString&) {}).appendChoice({}, 0).appendTabstop();
        QCOMPARE(snippet.value, QStringLiteral("${99:raw}${VAR}${0||}$1"));
        snippet.value = QStringLiteral("reset");
        snippet.appendTabstop();
        QCOMPARE(snippet.value, QStringLiteral("reset$2"));
        SnippetString copy = snippet;
        copy.appendTabstop();
        QCOMPARE(copy.value, QStringLiteral("reset$2$3"));
        QCOMPARE(snippet.value, QStringLiteral("reset$2"));
    }
    void snippetEditsKeepRangeAndMutableOptionalWhitespace() {
        auto edit = SnippetTextEdit::insert(Position(2, 3), SnippetString("$1"));
        QVERIFY(edit.range.isEmpty());
        QVERIFY(edit.range.start().isEqual(Position(2, 3)));
        QVERIFY(!edit.keepWhitespace.has_value());
        edit.keepWhitespace = false;
        edit.snippet.appendTabstop();
        QCOMPARE(edit.snippet.value, QStringLiteral("$1$1"));
        auto replace = SnippetTextEdit::replace(Range(0, 0, 0, 2), SnippetString("new"));
        QVERIFY(replace.range.isEqual(Range(0, 0, 0, 2)));
        QCOMPARE(replace.snippet.value, QStringLiteral("new"));
    }
    void workspaceEditPreservesUpstreamAppendAndTextOnlyQuerySemantics() {
        WorkspaceEdit edit;
        const QUrl first("file:///first.txt"), second("file:///second.txt");
        edit.createFile(first);
        QCOMPARE(edit.size(), qsizetype(0));
        QVERIFY(!edit.has(first));
        edit.insert(first, Position(0, 0), "first");
        edit.set(first, QVector<TextEdit>{TextEdit::insert(Position(0, 0), "second")});
        QCOMPARE(edit.get(first).size(), 2);
        QCOMPARE(edit.get(first)[0].newText, QString("first"));
        QCOMPARE(edit.get(first)[1].newText, QString("second"));
        edit.replace(second, Range(0, 0, 0, 1), "third");
        QCOMPARE(edit.size(), qsizetype(2));
        QCOMPARE(edit.entries().first().first, first);
        edit.set(first, std::nullopt);
        QVERIFY(!edit.has(first));
        QCOMPARE(edit.size(), qsizetype(1));
        // An empty set is an append of no entries, not a clear operation.
        edit.set(second, QVector<TextEdit>{});
        QVERIFY(edit.has(second));
    }
    void disposableFromRunsEveryCallbackOnce() {
        int first = 0, second = 0;
        std::vector<Disposable> parts;
        parts.emplace_back([&] { ++first; });
        parts.emplace_back([&] { ++second; });
        auto combined = Disposable::from(std::move(parts));
        combined.dispose();
        combined.dispose();
        QCOMPARE(first, 1);
        QCOMPARE(second, 1);
    }
    void eventEmitterDeliversInOrderAndIsolatesFailures() {
        EventEmitter<int> emitter;
        QStringList received;
        auto first = emitter.event()([&](int value) {
            received.append("first:" + QString::number(value));
            throw std::runtime_error("listener failed");
        });
        auto second = emitter.event()([&](int value) {
            received.append("second:" + QString::number(value));
        });
        emitter.fire(7);
        QCOMPARE(received, QStringList({"first:7", "second:7"}));
        first.dispose();
        emitter.fire(8);
        QCOMPARE(received.last(), QString("second:8"));
        QCOMPARE(received.size(), 3);
        emitter.dispose();
        emitter.fire(9);
        QVERIFY(!emitter.event()([](int) {}));
        QCOMPARE(received.size(), 3);
        second.dispose();
    }
    void cancellationFiresOnceAndLateSubscribersFireNextTurn() {
        CancellationTokenSource source;
        const auto token = source.token();
        QVERIFY(!token.isCancellationRequested());
        int calls = 0;
        auto listener = token.onCancellationRequested([&] { ++calls; });
        source.cancel();
        source.cancel();
        QVERIFY(token.isCancellationRequested());
        QCOMPARE(calls, 1);
        int late = 0;
        auto lateListener = token.onCancellationRequested([&] { ++late; });
        QCOMPARE(late, 0);
        QTRY_COMPARE(late, 1);
        auto disposedLate = token.onCancellationRequested([&] { ++late; });
        disposedLate.dispose();
        QCoreApplication::processEvents();
        QCOMPARE(late, 1);
        listener.dispose();
        lateListener.dispose();
    }
    void disposingSourceDoesNotCancelToken() {
        CancellationToken token;
        int calls = 0;
        {
            CancellationTokenSource source;
            token = source.token();
            auto listener = token.onCancellationRequested([&] { ++calls; });
            source.dispose();
            QVERIFY(!token.isCancellationRequested());
            source.cancel();
            QVERIFY(token.isCancellationRequested());
            QCOMPARE(calls, 0);
            listener.dispose();
        }
        QVERIFY(token.isCancellationRequested());
        const CancellationError error;
        QCOMPARE(QString::fromLatin1(error.what()), QString("Canceled"));
    }
};

QTEST_GUILESS_MAIN(ExtensionPrimitiveTests)
#include "tst_extension_primitives.moc"
