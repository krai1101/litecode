#pragma once
#include "extensions/HostDocuments.h"
#include "extensions/HostWorkspaceRequests.h"
#include <QMutex>
#include <QPointer>

namespace litecode::extensions {
// Host dispatcher owns the URI loader cache. Callers may be extension workers;
// their futures are registered before dispatch and rejected on host shutdown.
class HostDocumentOpening final : public QObject {
  public:
    HostDocumentOpening(HostDocuments& documents, HostWorkspaceRequests& requests);
    ~HostDocumentOpening() override;
    QFuture<std::shared_ptr<TextDocument>> open(const QUrl& uri, const TextDocumentOpenOptions& options);
    QFuture<std::shared_ptr<TextDocument>> create(const UntitledTextDocumentOptions& options);
    void shutdown();
  private:
    using Promise = QPromise<std::shared_ptr<TextDocument>>;
    struct Caller { quint64 id; QUrl uri; TextDocumentOpenOptions options; bool joined{}; };
    struct Loader { QVector<Caller> callers; };
    std::pair<quint64, QFuture<std::shared_ptr<TextDocument>>> registerRequest();
    void start(Caller caller);
    void resolve(quint64 id, std::shared_ptr<TextDocument> document, std::exception_ptr error = {});
    void watchCreation(quint64 id, QFuture<QString> future);
    QPointer<HostDocuments> documents_;
    QPointer<HostWorkspaceRequests> requests_;
    QHash<QString, Loader> loaders_; // Host thread only.
    QMutex mutex_;
    QHash<quint64, std::shared_ptr<Promise>> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
