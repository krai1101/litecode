#include "extensions/HostContentProviders.h"
#include "extensions/TextContentWire.h"
#include <QFutureWatcher>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace litecode::extensions {
namespace {
// The pin checks Object.keys(Schemas), not its values. Preserve that rule;
// do not reserve unrelated names or force a LiteCode-specific naming scheme.
bool reserved(const QString& scheme) {
    static const QSet<QString> names{"inMemory",
                                     "vscode",
                                     "internal",
                                     "walkThrough",
                                     "walkThroughSnippet",
                                     "vscodeOnboardingSample",
                                     "http",
                                     "https",
                                     "file",
                                     "mailto",
                                     "untitled",
                                     "data",
                                     "command",
                                     "vscodeRemote",
                                     "vscodeRemoteResource",
                                     "vscodeManagedRemoteResource",
                                     "vscodeUserData",
                                     "vscodeCustomEditor",
                                     "vscodeNotebookCell",
                                     "vscodeNotebookCellMetadata",
                                     "vscodeNotebookCellMetadataDiff",
                                     "vscodeNotebookCellOutput",
                                     "vscodeNotebookCellOutputDiff",
                                     "vscodeNotebookMetadata",
                                     "vscodeInteractiveInput",
                                     "vscodeSettings",
                                     "vscodeWorkspaceTrust",
                                     "vscodeTerminal",
                                     "vscodeImageCarousel",
                                     "vscodeChatCodeBlock",
                                     "vscodeChatCodeCompareBlock",
                                     "vscodeChatEditor",
                                     "vscodeChatInput",
                                     "sessionsChatInput",
                                     "vscodeLocalChatSession",
                                     "vscodeChatResponseResource",
                                     "agentHostTerminal",
                                     "vscodeChatTerminalOutput",
                                     "webviewPanel",
                                     "vscodeWebview",
                                     "vscodeBrowser",
                                     "extension",
                                     "vscodeFileResource",
                                     "tmp",
                                     "vsls",
                                     "vscodeSourceControl",
                                     "commentsInput",
                                     "codeSetting",
                                     "outputChannel",
                                     "accessibleView",
                                     "chatEditingSnapshotScheme",
                                     "chatEditingModel",
                                     "copilotPr"};
    return names.contains(scheme);
}
QFuture<std::optional<QString>> ready(std::optional<QString> text) {
    QPromise<std::optional<QString>> promise;
    promise.start();
    promise.addResult(std::move(text));
    promise.finish();
    return promise.future();
}
QString failure() {
    try {
        throw;
    } catch (const std::exception& error) {
        return QString::fromUtf8(error.what());
    } catch (...) {
        return QStringLiteral("Text document content provider failed.");
    }
}
} // namespace
HostContentProviders::HostContentProviders(HostDocuments& documents,
                                           std::function<void(const QJsonObject&)> send)
    : documents_(&documents), send_(std::move(send)) {}
HostContentProviders::~HostContentProviders() { shutdown(); }
bool HostContentProviders::stopped() const {
    QMutexLocker lock(&mutex_);
    return stopped_;
}
Disposable
HostContentProviders::registerProvider(const QString& scheme,
                                       std::shared_ptr<TextDocumentContentProvider> provider) {
    if (!provider)
        throw std::invalid_argument("A text document content provider is required.");
    if (reserved(scheme))
        throw std::invalid_argument("URI scheme is already registered by the workbench.");
    auto registration = std::make_shared<Registration>();
    registration->scheme = scheme;
    registration->provider = std::move(provider);
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || nextHandle_ == std::numeric_limits<quint64>::max())
            throw std::runtime_error("Content provider service unavailable.");
        registration->handle = ++nextHandle_;
        registrations_.insert(registration->handle, registration);
    }
    const QPointer<HostContentProviders> alive(this);
    const auto publish = [alive, registration] {
        if (alive && !alive->stopped())
            alive->send_({{"event", "registerTextContentProvider"},
                          {"handle", QString::number(registration->handle)},
                          {"scheme", registration->scheme}});
    };
    if (QThread::currentThread() == thread())
        publish();
    else
        QMetaObject::invokeMethod(this, publish, Qt::QueuedConnection);
    Disposable subscription;
    if (registration->provider->onDidChange) {
        const std::weak_ptr<Registration> weak(registration);
        subscription = (*registration->provider->onDidChange)([alive, weak](const QUrl& uri) {
            const auto registration = weak.lock();
            if (alive && registration)
                QMetaObject::invokeMethod(
                    alive,
                    [alive, registration, uri] {
                        if (alive && !alive->stopped())
                            alive->changed(registration, uri);
                    },
                    Qt::QueuedConnection);
        });
    }
    {
        QMutexLocker lock(&mutex_);
        if (!stopped_ && registrations_.contains(registration->handle))
            registration->subscription = std::move(subscription);
    }
    subscription.dispose(); // Shutdown can race registration on an extension worker.
    return Disposable([alive, handle = registration->handle] {
        if (alive)
            alive->unregister(handle);
    });
}
void HostContentProviders::unregister(quint64 handle) {
    std::shared_ptr<Registration> registration;
    {
        QMutexLocker lock(&mutex_);
        registration = registrations_.take(handle);
    }
    if (!registration)
        return;
    registration->subscription.dispose();
    const QPointer<HostContentProviders> alive(this);
    const auto publish = [alive, handle] {
        if (alive && !alive->stopped())
            alive->send_(
                {{"event", "unregisterTextContentProvider"}, {"handle", QString::number(handle)}});
    };
    if (QThread::currentThread() == thread())
        publish();
    else
        QMetaObject::invokeMethod(this, publish, Qt::QueuedConnection);
}
QFuture<std::optional<QString>>
HostContentProviders::invoke(const std::shared_ptr<Registration>& registration, const QUrl& uri) {
    try {
        // Upstream currently supplies CancellationToken.None for this contract.
        auto result = registration->provider->provideTextDocumentContent(uri, CancellationToken{});
        return std::visit(
            [](auto&& value) -> QFuture<std::optional<QString>> {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, QString>)
                    return ready(std::optional<QString>(std::move(value)));
                else if constexpr (std::is_same_v<T, std::optional<QString>>)
                    return ready(std::move(value));
                else if constexpr (std::is_same_v<T, QFuture<QString>>)
                    return value.then(
                        [](QString text) { return std::optional<QString>(std::move(text)); });
                else
                    return value;
            },
            std::move(result));
    } catch (...) {
        QPromise<std::optional<QString>> promise;
        promise.start();
        promise.setException(std::current_exception());
        promise.finish();
        return promise.future();
    }
}
void HostContentProviders::provide(quint64 handle, quint64 request, const QUrl& uri) {
    std::shared_ptr<Registration> registration;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        registration = registrations_.value(handle);
    }
    if (!registration || registration->scheme != uri.scheme()) {
        sendResult(handle, request, uri, {}, "Unsupported text document URI scheme.", false);
        return;
    }
    auto* watcher = new QFutureWatcher<std::optional<QString>>(this);
    connect(watcher, &QFutureWatcher<std::optional<QString>>::finished, this,
            [this, watcher, registration, request, uri] {
                try {
                    if (watcher->future().isCanceled())
                        throw std::runtime_error("Text content provider future canceled.");
                    sendResult(registration->handle, request, uri, watcher->result(), {}, false);
                } catch (...) {
                    sendResult(registration->handle, request, uri, {}, failure(), false);
                }
                watcher->deleteLater();
            });
    watcher->setFuture(invoke(registration, uri));
}
void HostContentProviders::changed(const std::shared_ptr<Registration>& registration,
                                   const QUrl& uri) {
    if (uri.scheme() != registration->scheme || !documents_)
        return;
    const auto documents = documents_->textDocuments();
    if (std::none_of(documents.begin(), documents.end(),
                     [&uri](const auto& document) { return document->uri() == uri; }))
        return;
    registration->changes.enqueue(uri);
    nextChange(registration);
}
void HostContentProviders::nextChange(const std::shared_ptr<Registration>& registration) {
    if (stopped() || registration->changing || registration->changes.isEmpty())
        return;
    const auto uri = registration->changes.dequeue();
    registration->changing = true;
    auto* watcher = new QFutureWatcher<std::optional<QString>>(this);
    connect(watcher, &QFutureWatcher<std::optional<QString>>::finished, this,
            [this, watcher, registration, uri] {
                try {
                    if (!watcher->future().isCanceled()) {
                        const auto text = watcher->result();
                        if (text && documents_)
                            for (const auto& document : documents_->textDocuments()) {
                                if (document->uri() != uri)
                                    continue;
                                static const QRegularExpression eol(
                                    QStringLiteral("\\r\\n|\\r|\\n"));
                                if (document->getText().split(eol) != text->split(eol))
                                    sendResult(registration->handle, 0, uri, text, {}, true);
                                break;
                            }
                    }
                } catch (...) {
                    qWarning("Text document content update failed: %s", qPrintable(failure()));
                }
                watcher->deleteLater();
                registration->changing = false;
                nextChange(registration);
            });
    watcher->setFuture(invoke(registration, uri));
}
void HostContentProviders::sendResult(quint64 handle, quint64 request, const QUrl& uri,
                                      std::optional<QString> text, QString error, bool change) {
    if (stopped())
        return;
    if (text && text->size() > wire::TextContentDecoder::maximumLength()) {
        if (change) {
            qWarning("Text document content update exceeds synchronization limit.");
            return;
        }
        text.reset();
        error = QStringLiteral("Text document content exceeds synchronization limit.");
    }
    if (nextTransfer_ == std::numeric_limits<quint64>::max())
        throw std::overflow_error("Text content transfer identifiers exhausted.");
    wire::sendTextContent(
        {++nextTransfer_, handle, request, uri, std::move(text), error.left(8192), change}, send_);
}
void HostContentProviders::shutdown() {
    QHash<quint64, std::shared_ptr<Registration>> registrations;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_)
            return;
        stopped_ = true;
        registrations = std::exchange(registrations_, {});
    }
    for (const auto& registration : registrations)
        registration->subscription.dispose();
}
} // namespace litecode::extensions
