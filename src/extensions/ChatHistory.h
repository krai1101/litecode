#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace litecode::extensions {

// Workbench-owned history for one chat session. The wire representation keeps
// the fields of VS Code's ChatRequestTurn and ChatResponseTurn, but this is not
// a public SDK type. A separate instance is required for each chat session.
class ChatHistory final {
  public:
    [[nodiscard]] QJsonArray history(const QString& participantId) const;
    bool record(const QString& participantId, const QJsonObject& request,
                const QJsonArray& responseParts, const QJsonObject& result);
    void clear();

  private:
    QHash<QString, QJsonArray> turnsByParticipant_;
};

} // namespace litecode::extensions
