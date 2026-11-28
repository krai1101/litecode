#include "CodexExecutionPolicy.h"
#include <QJsonArray>
namespace litecode::codex {
QJsonObject codexApprovalPolicy(ApprovalMode mode, bool startingThread) {
    const bool full = mode == ApprovalMode::FullAccess;
    QJsonObject result{
        {"approvalPolicy", full ? "never" : "on-request"},
        {"approvalsReviewer", mode == ApprovalMode::AutoReview ? "guardian_subagent" : "user"}};
    if (startingThread)
        result.insert("sandbox", full ? "danger-full-access" : "workspace-write");
    else
        result.insert("sandboxPolicy", full ? QJsonObject{{"type", "dangerFullAccess"}}
                                            : QJsonObject{{"type", "workspaceWrite"},
                                                          {"writableRoots", QJsonArray{}},
                                                          {"networkAccess", false},
                                                          {"excludeTmpdirEnvVar", false},
                                                          {"excludeSlashTmp", false}});
    return result;
}
QJsonObject codexCollaborationMode(bool plan, const QString& model, const QString& effort) {
    return {{"mode", plan ? "plan" : "default"},
            {"settings",
             QJsonObject{{"model", model},
                         {"reasoning_effort",
                          effort.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(effort)},
                         {"developer_instructions", QJsonValue(QJsonValue::Null)}}}};
}
} // namespace litecode::codex
