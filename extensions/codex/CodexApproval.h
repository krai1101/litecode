#pragma once
#include <extensions/MessageApi.h>
#include <QJsonObject>
#include <QJsonValue>
#include <QVector>

namespace litecode::codex {
struct CodexApprovalChoice : extensions::MessageItem {
    QJsonValue decision;
};
struct CodexApprovalPrompt {
    QString title, detail, category, preview;
    QVector<CodexApprovalChoice> choices;
};
CodexApprovalPrompt codexApprovalPrompt(const QString& method, const QJsonObject& params);
QJsonObject codexApprovalResponse(const QString& method, const QJsonObject& params,
                                const std::optional<CodexApprovalChoice>& choice);
} // namespace litecode::codex
