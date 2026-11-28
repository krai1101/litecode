#pragma once
#include <QHash>
#include <extensions/ContentProviderApi.h>
namespace litecode::codex {
class CodexDiffDocuments final : public extensions::TextDocumentContentProvider {
  public:
    QUrl add(const QString& turn, const QString& path, bool before, const QByteArray& bytes);
    extensions::ProviderResult<QString>
    provideTextDocumentContent(const QUrl& uri, extensions::CancellationToken token) override;

  private:
    QHash<QString, QString> contents_;
};
} // namespace litecode::codex
