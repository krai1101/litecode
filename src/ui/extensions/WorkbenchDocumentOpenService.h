#pragma once
#include "extensions/DocumentOpenApi.h"
#include "extensions/DocumentWire.h"
#include "editor/BoundDocumentReferences.h"
#include "extensions/WorkbenchContentProviders.h"
#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QHash>
#include <QQueue>
#include <QSet>

namespace litecode::ui {
class EditorArea;
// Serializes decode requests per model; disk workers never own UI objects.
class WorkbenchDocumentOpenService final : public QObject {
  public:
    explicit WorkbenchDocumentOpenService(EditorArea* editors, QObject* parent = nullptr,
        editor::DocumentReferenceLimits limits = {});
    ~WorkbenchDocumentOpenService() override;
    QFuture<extensions::TextDocumentSnapshot> open(const QUrl& uri,
        const extensions::TextDocumentOpenOptions& options, const QString& owner = {});
    QFuture<extensions::TextDocumentSnapshot> create(const extensions::UntitledTextDocumentOptions& options,
        const QString& owner = {});
    void removeOwner(const QString& owner);
    void setContentProviders(extensions::WorkbenchContentProviders* providers) { contentProviders_ = providers; }
  private:
    struct Request {
        std::shared_ptr<QPromise<extensions::TextDocumentSnapshot>> promise;
        std::optional<QString> encoding;
        QString owner;
    };
    void process(quint64 id);
    void finish(quint64 id, const QString& error = {});
    void retain(const QString& owner, const extensions::TextDocumentSnapshot& snapshot);
    void readVirtual(quint64 id);
    QPointer<EditorArea> editors_;
    QHash<quint64, QQueue<Request>> pending_;
    QSet<quint64> running_;
    QHash<QString, editor::BoundDocumentReferences*> references_;
    editor::DocumentReferenceLimits limits_;
    QPointer<extensions::WorkbenchContentProviders> contentProviders_;
};
} // namespace litecode::ui
