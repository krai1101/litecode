#pragma once
#include "editor/DocumentSession.h"
#include <QUrl>
#include <utility>

namespace litecode::editor {
// Workbench-owned text and disk state, independent of tabs/Scintilla widgets.
// Access is serialized by its owning workbench service, not extension threads.
class TextDocumentModel final {
  public:
    TextDocumentModel(core::DocumentId id, QString path, QString languageId = QStringLiteral("plaintext"));
    bool acceptLoad(const DocumentLoadResult& load);
    bool initializeUntitled(const QString& content, core::TextEncoding encoding = core::TextEncoding::Utf8,
        core::LineEnding lineEnding = core::LineEnding::None);
    bool initializeVirtual(const QUrl& resource, const QString& content);
    void bindVirtualResource(const QUrl& resource);
    bool acceptReload(const DocumentLoadResult& load);
    bool applyEdit(core::DocumentEdit* edit);
    bool applyProviderEdit(core::DocumentEdit* edit);
    bool applyEdits(qint64 expectedVersion, QVector<core::DocumentEdit>* edits,
        bool provider = false, QString* error = nullptr);
    bool save(QString* error);
    bool replaceWithLineEnding(const QByteArray& text, core::LineEnding eol, core::DocumentEditReason reason,
        std::optional<core::DocumentEdit>* applied, QString* error = nullptr);
    bool saveAs(const QString& path, core::DocumentPathChange* change, QString* error);
    [[nodiscard]] const QByteArray& contents() const { return contents_; }
    [[nodiscard]] bool hasTextBuffer() const { return session_.storageMode() == core::DocumentStorageMode::Normal; }
    [[nodiscard]] DocumentSession& session() { return session_; }
    [[nodiscard]] const DocumentSession& session() const { return session_; }
    [[nodiscard]] QUrl resource() const;
    void setUntitledResource(QUrl uri);
    [[nodiscard]] const QString& languageId() const { return languageId_; }
    void setLanguageId(QString languageId) { languageId_ = std::move(languageId); }
  private:
    bool applyEditImpl(core::DocumentEdit* edit, bool provider);
    DocumentSession session_;
    QByteArray contents_;
    QString languageId_;
    QUrl resourceOverride_;
};
} // namespace litecode::editor
