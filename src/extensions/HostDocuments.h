#pragma once
#include "extensions/ExtensionApi.h"
#include "extensions/HostTextDocument.h"
#include <QHash>
#include <QObject>
#include <QMutex>

namespace litecode::extensions {
// Private lifecycle owner for live document mirrors. Workbench messages update
// this service; a plugin cannot mutate a document by changing a snapshot.
class HostDocuments final : public QObject {
  public:
    explicit HostDocuments(std::function<QFuture<bool>(const QString&)> save, QObject* parent = nullptr);
    ~HostDocuments() override;
    [[nodiscard]] QVector<std::shared_ptr<TextDocument>> textDocuments() const;
    [[nodiscard]] std::shared_ptr<TextDocument> find(const QString& id) const;
    [[nodiscard]] Event<std::shared_ptr<TextDocument>> onDidOpenTextDocument() const { return opened_.event(); }
    [[nodiscard]] Event<TextDocumentChangeEvent> onDidChangeTextDocument() const { return changed_.event(); }
    [[nodiscard]] Event<std::shared_ptr<TextDocument>> onDidCloseTextDocument() const { return closed_.event(); }
    [[nodiscard]] Event<std::shared_ptr<TextDocument>> onDidSaveTextDocument() const { return saved_.event(); }
    void accept(TextDocumentSnapshot snapshot, bool initial = false);
    void metadata(const TextDocumentMetadata& metadata);
    void change(const QString& id, qint64 previousVersion, qint64 version, const Range& range, const QString& text, bool dirty,
        std::optional<TextDocumentChangeReason> reason = {});
    void close(const QString& id);
    void changeBatch(const wire::DocumentBatch& batch);
    void saved(const QString& id);
    void shutdown();
  private:
    mutable QMutex mutex_;
    QHash<QString, std::shared_ptr<HostTextDocument>> documents_;
    std::function<QFuture<bool>(const QString&)> save_;
    EventEmitter<std::shared_ptr<TextDocument>> opened_, closed_, saved_;
    EventEmitter<TextDocumentChangeEvent> changed_;
    bool stopped_{};
};
} // namespace litecode::extensions
