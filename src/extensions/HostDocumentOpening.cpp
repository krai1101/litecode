#include "extensions/HostDocumentOpening.h"
#include <QFutureWatcher>
#include <QMutexLocker>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
HostDocumentOpening::HostDocumentOpening(HostDocuments& documents, HostWorkspaceRequests& requests)
    : documents_(&documents), requests_(&requests) {}
HostDocumentOpening::~HostDocumentOpening() { shutdown(); }
std::pair<quint64, QFuture<std::shared_ptr<TextDocument>>> HostDocumentOpening::registerRequest() {
    auto promise = std::make_shared<Promise>();
    promise->start();
    const auto future = promise->future();
    QMutexLocker lock(&mutex_);
    if (stopped_ || nextId_ == std::numeric_limits<quint64>::max()) {
        promise->setException(
            std::make_exception_ptr(std::runtime_error("Document opening service stopped.")));
        promise->finish();
        return {0, future};
    }
    const auto id = ++nextId_;
    pending_.insert(id, std::move(promise));
    return {id, future};
}
QFuture<std::shared_ptr<TextDocument>>
HostDocumentOpening::open(const QUrl& uri, const TextDocumentOpenOptions& options) {
    const auto [id, future] = registerRequest();
    if (id)
        QMetaObject::invokeMethod(
            this, [this, id, uri, options] { start({id, uri, options}); }, Qt::QueuedConnection);
    return future;
}
QFuture<std::shared_ptr<TextDocument>>
HostDocumentOpening::create(const UntitledTextDocumentOptions& options) {
    const auto [id, future] = registerRequest();
    if (id)
        QMetaObject::invokeMethod(
            this,
            [this, id, options] {
                {
                    QMutexLocker lock(&mutex_);
                    if (stopped_ || !pending_.contains(id))
                        return;
                }
                try {
                    if (!requests_)
                        throw std::runtime_error("Workspace request service stopped.");
                    watchCreation(id, requests_->create(options));
                } catch (...) {
                    resolve(id, {}, std::current_exception());
                }
            },
            Qt::QueuedConnection);
    return future;
}
void HostDocumentOpening::start(Caller caller) {
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || !pending_.contains(caller.id))
            return;
    }
    try {
        if (!documents_ || !requests_)
            throw std::runtime_error("Document service stopped.");
        if (!caller.uri.isValid() || caller.uri.scheme().isEmpty())
            throw std::invalid_argument("Invalid document URI.");
        for (const auto& document : documents_->textDocuments()) {
            if (!document->isClosed() && document->uri() == caller.uri &&
                (!caller.options.encoding || caller.options.encoding->isEmpty() ||
                 *caller.options.encoding == document->encoding())) {
                resolve(caller.id, document);
                return;
            }
        }
        const auto key = caller.uri.toString(QUrl::FullyEncoded);
        auto found = loaders_.find(key);
        if (found != loaders_.end()) {
            caller.joined = true;
            found->callers.append(std::move(caller));
            return;
        }
        loaders_.insert(key, Loader{{caller}});
        auto* watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, key] {
            const auto loader = loaders_.take(key);
            std::shared_ptr<TextDocument> document;
            std::exception_ptr error;
            try {
                if (watcher->future().isCanceled())
                    throw std::runtime_error("Document opening canceled.");
                const auto id = watcher->result();
                document = documents_ ? documents_->find(id) : nullptr;
                if (!document || document->isClosed())
                    throw std::runtime_error("Opened document is no longer synchronized.");
            } catch (...) {
                error = std::current_exception();
            }
            watcher->deleteLater();
            for (auto caller : loader.callers) {
                if (!error && caller.joined && caller.options.encoding &&
                    !caller.options.encoding->isEmpty() &&
                    document->encoding() != *caller.options.encoding) {
                    caller.joined = false;
                    start(std::move(caller));
                } else
                    resolve(caller.id, document, error);
            }
        });
        try {
            watcher->setFuture(requests_->open(caller.uri, caller.options));
        } catch (...) {
            watcher->deleteLater();
            const auto loader = loaders_.take(key);
            const auto error = std::current_exception();
            for (const auto& waiting : loader.callers)
                resolve(waiting.id, {}, error);
        }
    } catch (...) {
        resolve(caller.id, {}, std::current_exception());
    }
}
void HostDocumentOpening::watchCreation(quint64 id, QFuture<QString> future) {
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, id, watcher] {
        try {
            if (watcher->future().isCanceled())
                throw std::runtime_error("Document creation canceled.");
            const auto identity = watcher->result();
            const auto document = documents_ ? documents_->find(identity) : nullptr;
            if (!document || document->isClosed())
                throw std::runtime_error("Created document is no longer synchronized.");
            resolve(id, document);
        } catch (...) {
            resolve(id, {}, std::current_exception());
        }
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}
void HostDocumentOpening::resolve(quint64 id, std::shared_ptr<TextDocument> document,
                                  std::exception_ptr error) {
    std::shared_ptr<Promise> promise;
    {
        QMutexLocker lock(&mutex_);
        promise = pending_.take(id);
    }
    if (!promise)
        return;
    if (error)
        promise->setException(error);
    else
        promise->addResult(std::move(document));
    promise->finish();
}
void HostDocumentOpening::shutdown() {
    QHash<quint64, std::shared_ptr<Promise>> pending;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        pending = std::exchange(pending_, {});
    }
    const auto error = std::make_exception_ptr(
        std::runtime_error("Extension host stopped while opening a document."));
    for (const auto& promise : pending) {
        promise->setException(error);
        promise->finish();
    }
}
} // namespace litecode::extensions
