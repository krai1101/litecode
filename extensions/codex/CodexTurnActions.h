#pragma once
#include "codex/ChatSession.h"
#include "codex/TurnUndo.h"
#include <extensions/ExtensionApi.h>
namespace litecode::codex {
class CodexTurnActions final : public QObject {
  public:
    CodexTurnActions(extensions::ExtensionContext& context, ChatSession& session,
                     std::function<void(QString)> notice, QObject* parent);
    void undoLast();
    QString reviewLast() const;
    QVector<TurnUndo::Snapshot> reviewSnapshots(const QString& turn, const QString& diff,
                                               const QStringList& paths) const;
    void editPrompt(std::function<void(QString, QString, QString)> selected);

  private:
    QString recordPath(const QString& thread, const QString& turn) const;
    extensions::ExtensionContext& context_;
    ChatSession& session_;
    std::function<void(QString)> notice_;
};
} // namespace litecode::codex
