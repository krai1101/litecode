#include "extensions/SecretOperationQueue.h"
#include <QtTest>

using namespace litecode::extensions;
namespace {
QFuture<int> ready(int value) {
    QPromise<int> promise;
    promise.start();
    promise.addResult(value);
    promise.finish();
    return promise.future();
}
} // namespace
class ExtensionSecretQueueTests final : public QObject {
    Q_OBJECT
  private slots:
    void shutdownCallbacksCanReenterWithoutHoldingQueueMutex() {
        SecretOperationQueue queue(nullptr);
        int started = 0;
        auto pending = queue.enqueue<int>("key", [&] {
            ++started;
            return ready(1);
        });
        QFuture<int> nested;
        const auto recovered = pending.onFailed([&](const std::exception&) {
            queue.shutdown();
            nested = queue.enqueue<int>("key", [&] {
                ++started;
                return ready(2);
            });
            return 7;
        });
        queue.shutdown();
        QCOMPARE(recovered.result(), 7);
        QVERIFY(nested.isFinished());
        QVERIFY_THROWS_EXCEPTION(std::exception, nested.result());
        QCoreApplication::processEvents();
        QCOMPARE(started, 0);
    }
    void shutdownRejectsActiveAndQueuedOperationsAndIgnoresLateCompletion() {
        SecretOperationQueue queue(nullptr);
        QPromise<int> gate;
        gate.start();
        int started = 0;
        const auto first = queue.enqueue<int>("key", [&] {
            ++started;
            return gate.future();
        });
        const auto second = queue.enqueue<int>("key", [&] {
            ++started;
            return ready(2);
        });
        QTRY_COMPARE(started, 1);
        queue.shutdown();
        QVERIFY(first.isFinished());
        QVERIFY(second.isFinished());
        QVERIFY_THROWS_EXCEPTION(std::exception, first.result());
        QVERIFY_THROWS_EXCEPTION(std::exception, second.result());
        gate.addResult(1);
        gate.finish();
        QCoreApplication::processEvents();
        QCOMPARE(started, 1);
    }
    void completionCallbackCanDestroyQueue() {
        auto queue = std::make_unique<SecretOperationQueue>(nullptr);
        auto result = queue->enqueue<int>("key", [] { return ready(3); });
        const auto completed = result.then([&](int value) {
            queue.reset();
            return value;
        });
        QTRY_VERIFY(completed.isFinished());
        QCOMPARE(completed.result(), 3);
        QVERIFY(!queue);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};
QTEST_GUILESS_MAIN(ExtensionSecretQueueTests)
#include "tst_extension_secret_queue.moc"
