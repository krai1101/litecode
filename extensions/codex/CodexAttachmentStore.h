#pragma once
#include <QHash>
#include <QString>
namespace litecode::codex {
bool codexImageFile(const QString& path);
// Keeps image history independent of view-owned clipboard temporary files.
class CodexAttachmentStore {
  public:
    explicit CodexAttachmentStore(QString directory);
    QString imagePath(const QString& source);

  private:
    QString directory_;
    QHash<QString, QString> resolved_;
};
} // namespace litecode::codex
