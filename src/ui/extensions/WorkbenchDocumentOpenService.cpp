#include "ui/extensions/WorkbenchDocumentOpenService.h"
#include "editor/DocumentSession.h"
#include "editor/LanguageSupport.h"
#include "ui/EditorArea.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrentRun>
#include <stdexcept>
#include <utility>

namespace litecode::ui {
namespace {
using Snapshot = extensions::TextDocumentSnapshot;
void reject(const std::shared_ptr<QPromise<Snapshot>>& promise, const QString& error) {
    promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    promise->finish();
}
} // namespace
WorkbenchDocumentOpenService::WorkbenchDocumentOpenService(EditorArea* editors, QObject* parent,
                                                           editor::DocumentReferenceLimits limits)
    : QObject(parent), editors_(editors), limits_(limits) {
    connect(editors, &EditorArea::openFilesChanged, this, [this] {
        // Closing a loading tab can transfer reader ownership to this service.
        const auto ids = pending_.keys();
        for (const auto id : ids)
            if (!running_.contains(id))
                process(id);
    });
    connect(editors, &EditorArea::documentResourceRemoved, this, [this](const QUrl& resource) {
        QVector<QPointer<editor::BoundDocumentReferences>> collections;
        for (auto* collection : references_)
            collections.append(collection);
        for (const auto& collection : collections)
            if (collection)
                collection->remove(resource);
    });
    connect(editors, &EditorArea::documentOpened, this, [this](core::DocumentId id) {
        if (pending_.contains(id.value()) && !running_.contains(id.value()))
            process(id.value());
    });
    connect(editors, &EditorArea::documentClosed, this, [this](core::DocumentId id) {
        // Encoding reload currently removes/re-adds the host mirror while the
        // workbench model remains alive. That is not a model close.
        if (editors_ && editors_->documentModels().find(id))
            return;
        while (pending_.contains(id.value()))
            finish(id.value(), QStringLiteral("Document closed while opening."));
        QVector<QPointer<editor::BoundDocumentReferences>> collections;
        for (auto* collection : references_)
            collections.append(collection);
        for (const auto& collection : collections)
            if (collection)
                collection->remove(id);
    });
    connect(editors, &EditorArea::fileOpenFailed, this,
            [this](const QString&, const QString& error) {
                const auto ids = pending_.keys();
                for (const auto id : ids)
                    if (!editors_ || !editors_->documentModels().find(core::DocumentId(id)))
                        while (pending_.contains(id))
                            finish(id, error);
            });
    connect(editors, &QObject::destroyed, this, [this] {
        const auto ids = pending_.keys();
        for (const auto id : ids)
            while (pending_.contains(id))
                finish(id, QStringLiteral("Workbench document service stopped."));
    });
}
WorkbenchDocumentOpenService::~WorkbenchDocumentOpenService() {
    if (editors_)
        disconnect(editors_, nullptr, this, nullptr);
    for (const auto& queue : pending_)
        for (const auto& request : queue)
            reject(request.promise, QStringLiteral("Workbench document service stopped."));
    for (const auto id : pending_.keys())
        if (editors_)
            for (qsizetype index = 0; index < pending_.value(id).size(); ++index)
                editors_->releaseDocumentReference(core::DocumentId(id));
    qDeleteAll(std::exchange(references_, {}));
}
void WorkbenchDocumentOpenService::retain(const QString& owner, const Snapshot& snapshot) {
    const auto id = core::DocumentId(snapshot.id.toULongLong());
    auto* collection = references_.value(owner);
    if (!collection) {
        collection = new editor::BoundDocumentReferences(
            [editors = editors_](core::DocumentId document) {
                if (editors)
                    editors->releaseDocumentReference(document);
            },
            limits_, this);
        references_.insert(owner, collection);
    }
    editors_->documentModels().retain(id);
    try {
        collection->add(id, snapshot.uri, snapshot.text.size());
    } catch (...) {
        editors_->releaseDocumentReference(id);
        throw;
    }
}
void WorkbenchDocumentOpenService::removeOwner(const QString& owner) {
    // Remove queued ownership before settling futures: continuations may reenter.
    QVector<std::pair<quint64, Request>> removed;
    for (auto entry = pending_.begin(); entry != pending_.end();) {
        QQueue<Request> kept;
        for (const auto& request : entry.value()) {
            if (request.owner == owner)
                removed.append({entry.key(), request});
            else
                kept.enqueue(request);
        }
        if (kept.isEmpty())
            entry = pending_.erase(entry);
        else {
            entry.value() = std::move(kept);
            ++entry;
        }
    }
    delete references_.take(owner);
    for (const auto& [id, request] : removed) {
        reject(request.promise, QStringLiteral("Extension host stopped while opening a document."));
        if (editors_)
            editors_->releaseDocumentReference(core::DocumentId(id));
        if (!pending_.contains(id) && editors_ &&
            !editors_->hasDocumentEditor(core::DocumentId(id))) {
            const auto model = editors_->documentModels().find(core::DocumentId(id));
            if (model && model->session().state() == editor::DocumentState::Loading &&
                !editors_->documentModels().references(core::DocumentId(id)))
                editors_->documentModels().remove(core::DocumentId(id));
        }
        QTimer::singleShot(0, this, [this, id] { process(id); });
    }
}
QFuture<Snapshot>
WorkbenchDocumentOpenService::create(const extensions::UntitledTextDocumentOptions& options,
                                     const QString& owner) {
    auto promise = std::make_shared<QPromise<Snapshot>>();
    promise->start();
    const auto future = promise->future();
    try {
        if (!editors_)
            throw std::runtime_error("Workbench document service stopped.");
        auto model = editors_->documentModels().createUntitled(
            options.content.value_or(QString()),
            options.language.value_or(QStringLiteral("plaintext")),
            options.encoding ? core::textEncodingFromKey(*options.encoding)
                                   .value_or(editors_->documentDefaultEncoding())
                             : editors_->documentDefaultEncoding());
        model->session().setModified(true); // Upstream marks newly created host documents dirty.
        editors_->announceDocument(model->session().id());
        const auto snapshot = editors_->extensionDocumentSnapshot(model->session().id());
        if (!snapshot)
            throw std::runtime_error("Untitled document could not be synchronized.");
        retain(owner, *snapshot);
        promise->addResult(*snapshot);
        promise->finish();
    } catch (...) {
        promise->setException(std::current_exception());
        promise->finish();
    }
    return future;
}
QFuture<Snapshot> WorkbenchDocumentOpenService::open(
    const QUrl& uri, const extensions::TextDocumentOpenOptions& options, const QString& owner) {
    auto promise = std::make_shared<QPromise<Snapshot>>();
    promise->start();
    const auto future = promise->future();
    try {
        if (!editors_)
            throw std::runtime_error("Workbench document service stopped.");
        if (!uri.isValid() || uri.scheme().isEmpty() ||
            (uri.path().isEmpty() && uri.authority().isEmpty()))
            throw std::invalid_argument("Invalid document URI.");
        auto model = editors_->documentModels().find(uri);
        if (!model && uri.scheme() == QStringLiteral("untitled")) {
            const bool associated = uri.path().startsWith(QLatin1Char('/'));
            auto file = uri;
            file.setScheme(QStringLiteral("file"));
            if (associated && QFileInfo::exists(file.toLocalFile()))
                throw std::runtime_error("An existing file cannot be opened as untitled.");
            model = editors_->documentModels().createUntitled(
                {},
                associated ? editor::languageSupportForFile(file.toLocalFile()).id
                           : QStringLiteral("plaintext"),
                options.encoding ? core::textEncodingFromKey(*options.encoding)
                                       .value_or(editors_->documentDefaultEncoding())
                                 : editors_->documentDefaultEncoding());
            model->setUntitledResource(uri);
            model->session().setModified(true);
            editors_->announceDocument(model->session().id());
        }
        if (!model) {
            if (!uri.isLocalFile()) {
                if (!contentProviders_)
                    throw std::runtime_error("No text document provider for this URI scheme.");
                model = editors_->documentModels().createVirtual(uri);
            } else {
                const QFileInfo info(uri.toLocalFile());
                if (!info.exists() || !info.isFile())
                    throw std::runtime_error(
                        "Document file does not exist or is not a regular file.");
                model = editors_->documentModels().createFile(info.absoluteFilePath());
            }
        }
        const auto id = model->session().id().value();
        editors_->documentModels().retain(
            model->session().id()); // Protect the in-flight resolution.
        pending_[id].enqueue({promise, options.encoding, owner});
        if (!running_.contains(id))
            process(id);
    } catch (...) {
        promise->setException(std::current_exception());
        promise->finish();
    }
    return future;
}
void WorkbenchDocumentOpenService::process(quint64 id) {
    if (!pending_.contains(id) || running_.contains(id))
        return;
    if (!editors_) {
        finish(id, QStringLiteral("Workbench document service stopped."));
        return;
    }
    const auto model = editors_->documentModels().find(core::DocumentId(id));
    if (!model) {
        finish(id, QStringLiteral("Document is no longer open."));
        return;
    }
    const auto requested = pending_[id].head().encoding;
    const bool loading = model->session().state() == editor::DocumentState::Loading;
    const bool virtualDocument =
        model->resource().scheme() != "file" && model->resource().scheme() != "untitled";
    if (virtualDocument && requested && !requested->isEmpty()) {
        finish(id, QStringLiteral("Decoding encodings require a filesystem document."));
        return;
    }
    const auto fallback =
        loading ? editors_->documentDefaultEncoding() : model->session().defaultEncoding();
    const auto encoding =
        requested && !requested->isEmpty()
            ? std::optional(core::textEncodingFromKey(*requested).value_or(fallback))
            : std::nullopt;
    if (!loading && (!encoding || *encoding == model->session().encoding() ||
                     model->session().filePath().isEmpty())) {
        finish(id);
        return;
    }
    if (model->session().isModified()) {
        finish(id, QStringLiteral("A dirty document cannot be reopened with another encoding."));
        return;
    }
    if (virtualDocument) {
        readVirtual(id);
        return;
    }
    // A tab already loading this model owns its reader. Its documentOpened/
    // documentClosed event will resume or reject the queued request.
    if (loading && editors_->hasDocumentEditor(core::DocumentId(id)))
        return;
    running_.insert(id);
    const auto version = model->session().version();
    const auto requestPromise = pending_[id].head().promise;
    auto* watcher = new QFutureWatcher<editor::DocumentLoadResult>(this);
    connect(watcher, &QFutureWatcher<editor::DocumentLoadResult>::finished, this,
            [this, watcher, id, model, version, loading, requestPromise] {
                if (!pending_.contains(id) || pending_.value(id).head().promise != requestPromise) {
                    watcher->deleteLater();
                    running_.remove(id);
                    if (!pending_.contains(id) && editors_ &&
                        model->session().state() == editor::DocumentState::Loading &&
                        !editors_->hasDocumentEditor(core::DocumentId(id)))
                        editors_->documentModels().remove(core::DocumentId(id));
                    else
                        process(id);
                    return;
                }
                // A newly attached tab may have completed its reader first. Reuse
                // that model, or queue the requested re-decoding if still needed;
                // never overwrite subsequent edits with our older disk snapshot.
                if (loading && editors_ && !model->session().isClosed() &&
                    model->session().state() != editor::DocumentState::Loading) {
                    watcher->deleteLater();
                    running_.remove(id);
                    process(id);
                    return;
                }
                QString error;
                try {
                    const auto load = watcher->result();
                    if (!editors_)
                        error = QStringLiteral("Workbench document service stopped.");
                    else if (model->session().isClosed())
                        error = QStringLiteral("Document closed while opening.");
                    else if (model->session().version() != version || model->session().isModified())
                        error = QStringLiteral("Document changed while opening.");
                    else if (load.storageMode != core::DocumentStorageMode::Normal)
                        error =
                            QStringLiteral("Document is too large for extension synchronization.");
                    else if (!editors_->acceptDocumentLoad(core::DocumentId(id), load, &error) &&
                             error.isEmpty())
                        error = QStringLiteral("Document load could not be accepted.");
                } catch (const std::exception& exception) {
                    error = QString::fromUtf8(exception.what());
                } catch (...) {
                    error = QStringLiteral("Document loading failed.");
                }
                watcher->deleteLater();
                running_.remove(id);
                finish(id, error);
            });
    const auto path = model->session().filePath();
    const auto guess = editors_->documentAutoGuessEncoding();
    watcher->setFuture(QtConcurrent::run([path, encoding, fallback, guess] {
        return editor::DocumentSession::loadBounded(path, encoding, fallback, guess);
    }));
}
void WorkbenchDocumentOpenService::finish(quint64 id, const QString& error) {
    auto found = pending_.find(id);
    if (found == pending_.end())
        return;
    const auto request = found->dequeue();
    if (found->isEmpty())
        pending_.erase(found);
    const auto snapshot =
        editors_ ? editors_->extensionDocumentSnapshot(core::DocumentId(id)) : std::nullopt;
    if (!error.isEmpty() || !snapshot)
        reject(request.promise,
               error.isEmpty() ? QStringLiteral("Document could not be synchronized.") : error);
    else {
        try {
            retain(request.owner, *snapshot);
            request.promise->addResult(*snapshot);
            request.promise->finish();
        } catch (...) {
            request.promise->setException(std::current_exception());
            request.promise->finish();
        }
    }
    if (editors_)
        editors_->releaseDocumentReference(core::DocumentId(id));
    if (!pending_.contains(id) && !error.isEmpty() && editors_) {
        const auto model = editors_->documentModels().find(core::DocumentId(id));
        if (model && model->session().state() == editor::DocumentState::Loading &&
            !editors_->hasDocumentEditor(core::DocumentId(id)))
            editors_->documentModels().remove(core::DocumentId(id));
    }
    QTimer::singleShot(0, this, [this, id] { process(id); });
}
void WorkbenchDocumentOpenService::readVirtual(quint64 id) {
    if (!contentProviders_) {
        finish(id, QStringLiteral("Content provider service stopped."));
        return;
    }
    const auto model = editors_->documentModels().find(core::DocumentId(id));
    const auto requestPromise = pending_[id].head().promise;
    running_.insert(id);
    auto* watcher = new QFutureWatcher<std::optional<QString>>(this);
    connect(
        watcher, &QFutureWatcher<std::optional<QString>>::finished, this,
        [this, watcher, model, id, requestPromise] {
            if (!pending_.contains(id)) {
                running_.remove(id);
                watcher->deleteLater();
                if (!pending_.contains(id) && editors_ &&
                    !editors_->hasDocumentEditor(core::DocumentId(id)))
                    editors_->documentModels().remove(core::DocumentId(id));
                else
                    process(id);
                return;
            }
            QString error;
            try {
                if (watcher->future().isCanceled())
                    throw std::runtime_error("Virtual document resolution canceled.");
                const auto text = watcher->result();
                if (!editors_ || model->session().isClosed())
                    throw std::runtime_error("Document closed while resolving content.");
                if (!text)
                    throw std::runtime_error("No content provider supplied text for this URI.");
                if (!model->initializeVirtual(model->resource(), *text))
                    throw std::runtime_error("Virtual document could not be initialized.");
                editors_->announceDocument(core::DocumentId(id));
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
            } catch (...) {
                error = QStringLiteral("Virtual document resolution failed.");
            }
            running_.remove(id);
            watcher->deleteLater();
            const bool sameCaller =
                pending_.contains(id) && pending_.value(id).head().promise == requestPromise;
            if (sameCaller || !error.isEmpty())
                finish(id, error);
            else
                process(
                    id); // Share resolved content with remaining owners, then honor their options.
        });
    watcher->setFuture(contentProviders_->provide(model->resource()));
}
} // namespace litecode::ui
