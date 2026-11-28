#include "extensions/WorkbenchContentProviders.h"
#include <QFutureWatcher>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
namespace {
void reject(const std::shared_ptr<QPromise<std::optional<QString>>>& promise,
            const QString& error) {
    if (promise->future().isFinished())
        return;
    promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    promise->finish();
}
} // namespace
QString WorkbenchContentProviders::key(const QString& owner, quint64 handle) {
    return owner + QChar(0) + QString::number(handle);
}
WorkbenchContentProviders::~WorkbenchContentProviders() {
    stopped_ = true;
    const auto pending = std::exchange(pending_, {});
    const auto resolutions = std::exchange(resolutions_, {});
    for (const auto& request : pending)
        reject(request.promise, "Content provider service stopped.");
    for (const auto& weak : resolutions)
        if (const auto resolution = weak.lock())
            reject(resolution->promise, "Content provider service stopped.");
}
bool WorkbenchContentProviders::registerProvider(const QString& owner, quint64 handle,
                                                 const QString& scheme) {
    if (stopped_ || owner.isEmpty() || !handle || known_.contains(key(owner, handle)))
        return false;
    known_.insert(key(owner, handle), scheme);
    providers_.prepend({owner, handle, scheme});
    return true;
}
bool WorkbenchContentProviders::unregisterProvider(const QString& owner, quint64 handle) {
    const auto found =
        std::find_if(providers_.begin(), providers_.end(), [&](const Provider& provider) {
            return provider.owner == owner && provider.handle == handle;
        });
    if (found == providers_.end())
        return false;
    providers_.erase(found);
    // Already invoked callbacks still own their provider until completion,
    // matching disposal of registration rather than aborting arbitrary futures.
    return true;
}
void WorkbenchContentProviders::removeOwner(const QString& owner) {
    providers_.erase(
        std::remove_if(providers_.begin(), providers_.end(),
                       [&owner](const Provider& provider) { return provider.owner == owner; }),
        providers_.end());
    for (auto entry = known_.begin(); entry != known_.end();) {
        if (entry.key().startsWith(owner + QChar(0)))
            entry = known_.erase(entry);
        else
            ++entry;
    }
    decoders_.remove(owner);
    QVector<quint64> requests;
    for (auto entry = pending_.cbegin(); entry != pending_.cend(); ++entry)
        if (entry->provider.owner == owner)
            requests.append(entry.key());
    for (const auto request : requests)
        complete(request, {}, "Extension host stopped during text content resolution.");
}
QFuture<std::optional<QString>> WorkbenchContentProviders::provide(const QUrl& uri) {
    auto resolution = std::make_shared<Resolution>();
    resolution->uri = uri;
    resolution->promise = std::make_shared<QPromise<std::optional<QString>>>();
    resolution->promise->start();
    const auto future = resolution->promise->future();
    if (stopped_) {
        reject(resolution->promise, "Content provider service stopped.");
        return future;
    }
    for (const auto& provider : providers_)
        if (provider.scheme == uri.scheme())
            resolution->providers.append(provider);
    resolutions_.erase(std::remove_if(resolutions_.begin(), resolutions_.end(),
                                      [](const auto& weak) { return weak.expired(); }),
                       resolutions_.end());
    resolutions_.append(resolution);
    next(resolution);
    return future;
}
void WorkbenchContentProviders::next(const std::shared_ptr<Resolution>& resolution) {
    if (stopped_) {
        reject(resolution->promise, "Content provider service stopped.");
        return;
    }
    while (resolution->index < resolution->providers.size()) {
        const auto provider = resolution->providers.at(resolution->index++);
        if (std::none_of(providers_.begin(), providers_.end(), [&provider](const Provider& active) {
                return active.owner == provider.owner && active.handle == provider.handle;
            }))
            continue;
        if (nextRequest_ == std::numeric_limits<quint64>::max()) {
            reject(resolution->promise, "Text content request identifiers exhausted.");
            return;
        }
        const auto request = ++nextRequest_;
        auto promise = std::make_shared<QPromise<std::optional<QString>>>();
        promise->start();
        const auto future = promise->future();
        pending_.insert(request, Pending{provider, resolution->uri, promise});
        auto* watcher = new QFutureWatcher<std::optional<QString>>(this);
        connect(watcher, &QFutureWatcher<std::optional<QString>>::finished, this,
                [this, watcher, resolution] {
                    try {
                        if (watcher->future().isCanceled())
                            throw std::runtime_error("Text content resolution canceled.");
                        const auto content = watcher->result();
                        if (content) {
                            resolution->promise->addResult(content);
                            resolution->promise->finish();
                        } else
                            next(resolution);
                    } catch (const std::exception& error) {
                        reject(resolution->promise, QString::fromUtf8(error.what()));
                    } catch (...) {
                        reject(resolution->promise, "Text content resolution failed.");
                    }
                    watcher->deleteLater();
                });
        watcher->setFuture(future);
        emit contentRequested(provider.owner, provider.handle, request, resolution->uri);
        return;
    }
    resolution->promise->addResult(std::optional<QString>{});
    resolution->promise->finish();
}
void WorkbenchContentProviders::complete(quint64 request, std::optional<QString> text,
                                         const QString& error) {
    auto found = pending_.find(request);
    if (found == pending_.end())
        return;
    const auto promise = found->promise;
    pending_.erase(found);
    if (!error.isEmpty())
        reject(promise, error);
    else {
        promise->addResult(std::move(text));
        promise->finish();
    }
}
void WorkbenchContentProviders::fail(quint64 request, const QString& error) {
    complete(request, {}, error);
}
bool WorkbenchContentProviders::accept(const QString& owner, const QJsonObject& message,
                                       QString* error) {
    QString diagnostic;
    const auto transfer = decoders_[owner].accept(message, &diagnostic);
    if (error)
        *error = diagnostic;
    if (!diagnostic.isEmpty())
        return false;
    if (!transfer)
        return true;
    if (transfer->change) {
        const auto scheme = known_.constFind(key(owner, transfer->handle));
        if (scheme == known_.cend() || *scheme != transfer->uri.scheme() || !transfer->content) {
            if (error)
                *error = "Unauthorized virtual document update.";
            return false;
        }
        emit virtualDocumentChanged(transfer->uri, *transfer->content);
    } else {
        const auto pending = pending_.constFind(transfer->requestId);
        if (pending == pending_.cend())
            return true; // Late completion after shutdown/cancellation.
        if (pending->provider.owner != owner || pending->provider.handle != transfer->handle ||
            pending->uri != transfer->uri) {
            if (error)
                *error = "Unauthorized text content response.";
            return false;
        }
        complete(transfer->requestId, transfer->content, transfer->error);
    }
    return true;
}
} // namespace litecode::extensions
