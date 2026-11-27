#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QQueue>
#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace litecode::extensions {

// Private counterpart to VS Code's SequencerByKey. An operation holds its
// position until both encryption and persistence finish, not just until the
// asynchronous keychain request has been dispatched.
class SecretOperationQueue final : public QObject {
  public:
    explicit SecretOperationQueue(QObject* parent) : QObject(parent) {}
    ~SecretOperationQueue() override { shutdown(); }

    template <typename T, typename F> QFuture<T> enqueue(const QString& key, F&& operation) {
        auto entry =
            std::make_shared<Entry<T>>(std::function<QFuture<T>()>(std::forward<F>(operation)));
        const auto future = entry->promise.future();
        bool stopped;
        {
            QMutexLocker lock(&mutex_);
            stopped = stopped_;
            if (!stopped) {
                // Track before posting so shutdown also rejects jobs not yet dispatched.
                pending_.append(entry);
                QMetaObject::invokeMethod(
                    this,
                    [this, key, entry] {
                        auto& queue = queues_[key];
                        queue.enqueue(entry);
                        if (queue.size() == 1)
                            start(key, entry);
                    },
                    Qt::QueuedConnection);
            }
        }
        // Finishing a promise can run extension continuations synchronously.
        // Never invoke them while holding the queue mutex.
        if (stopped)
            entry->reject();
        return future;
    }

    void shutdown() {
        QList<std::shared_ptr<EntryBase>> pending;
        {
            QMutexLocker lock(&mutex_);
            if (stopped_)
                return;
            stopped_ = true;
            pending.swap(pending_);
        }
        for (const auto& entry : pending)
            entry->reject();
    }

  private:
    struct EntryBase {
        virtual ~EntryBase() = default;
        virtual void start(QObject* context, std::function<void()> done) = 0;
        virtual void reject() = 0;
    };
    template <typename T> struct Entry final : EntryBase {
        explicit Entry(std::function<QFuture<T>()> operation) : operation(std::move(operation)) {
            promise.start();
        }
        void start(QObject* context, std::function<void()> done) override {
            if (finished.load()) {
                done();
                return;
            }
            auto* watcher = new QFutureWatcher<T>(context);
            const QPointer<QFutureWatcher<T>> watcherGuard(watcher);
            const QPointer<QObject> contextGuard(context);
            QObject::connect(watcher, &QFutureWatcher<T>::finished, context,
                             [this, watcherGuard, contextGuard, done] {
                                 if (!finished.exchange(true)) {
                                     try {
                                         auto future = watcherGuard->future();
                                         // Surface provider/persistence exceptions before treating
                                         // a failed Qt future's canceled state as user
                                         // cancellation.
                                         future.waitForFinished();
                                         if (future.isCanceled())
                                             throw std::runtime_error(
                                                 "Secret-storage operation was canceled.");
                                         if constexpr (!std::is_void_v<T>)
                                             promise.addResult(future.result());
                                     } catch (...) {
                                         promise.setException(std::current_exception());
                                     }
                                     promise.finish();
                                 }
                                 if (watcherGuard)
                                     watcherGuard->deleteLater();
                                 if (contextGuard)
                                     done();
                             });
            try {
                const auto future = operation();
                if (watcherGuard)
                    watcherGuard->setFuture(future);
            } catch (...) {
                if (!finished.exchange(true)) {
                    promise.setException(std::current_exception());
                    promise.finish();
                }
                if (watcherGuard)
                    watcherGuard->deleteLater();
                if (contextGuard)
                    done();
            }
        }
        void reject() override {
            if (finished.exchange(true))
                return;
            promise.setException(
                std::make_exception_ptr(std::runtime_error("Secret storage stopped.")));
            promise.finish();
        }
        QPromise<T> promise;
        std::function<QFuture<T>()> operation;
        std::atomic_bool finished{};
    };
    void start(const QString& key, const std::shared_ptr<EntryBase>& entry) {
        entry->start(this, [this, key, entry] {
            {
                QMutexLocker lock(&mutex_);
                pending_.removeOne(entry);
            }
            auto it = queues_.find(key);
            it->dequeue();
            if (it->isEmpty())
                queues_.erase(it);
            else {
                const auto next = it->head();
                QMetaObject::invokeMethod(
                    this, [this, key, next] { start(key, next); }, Qt::QueuedConnection);
            }
        });
    }
    QMutex mutex_;
    bool stopped_{};
    QList<std::shared_ptr<EntryBase>> pending_;
    QHash<QString, QQueue<std::shared_ptr<EntryBase>>> queues_;
};

} // namespace litecode::extensions
