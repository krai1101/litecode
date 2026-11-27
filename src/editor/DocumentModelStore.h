#pragma once
#include "editor/TextDocumentModel.h"
#include <QHash>
#include <QVector>
#include <memory>

namespace litecode::editor {
// Workbench ownership is separate from tabs. Lookup uses live model resources,
// so Save As/path changes cannot leave a stale URI index behind.
class DocumentModelStore final {
  public:
    explicit DocumentModelStore(core::DocumentIdGenerator& ids) : ids_(ids) {}
    std::shared_ptr<TextDocumentModel> createFile(const QString& path);
    std::shared_ptr<TextDocumentModel> createVirtual(const QUrl& uri, const std::optional<QString>& content = {});
    std::shared_ptr<TextDocumentModel> createUntitled(const QString& content = {},
        const QString& language = QStringLiteral("plaintext"),
        core::TextEncoding encoding = core::TextEncoding::Utf8,
        core::LineEnding lineEnding = core::LineEnding::None);
    std::shared_ptr<TextDocumentModel> find(core::DocumentId id) const;
    std::shared_ptr<TextDocumentModel> find(const QUrl& uri) const;
    QVector<std::shared_ptr<TextDocumentModel>> models() const;
    bool remove(core::DocumentId id);
    void retain(core::DocumentId id);
    void release(core::DocumentId id);
    [[nodiscard]] int references(core::DocumentId id) const { return references_.value(id.value()); }
  private:
    core::DocumentId nextId();
    core::DocumentIdGenerator& ids_;
    QHash<quint64, std::shared_ptr<TextDocumentModel>> models_;
    QHash<quint64, int> references_;
};
} // namespace litecode::editor
