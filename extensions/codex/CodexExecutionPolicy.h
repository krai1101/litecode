#pragma once
#include <QJsonObject>
namespace litecode::codex {
enum class ApprovalMode { Ask, AutoReview, FullAccess };
// App-server policy names stay at the protocol boundary, independent of UI labels.
QJsonObject codexApprovalPolicy(ApprovalMode mode, bool startingThread);
QJsonObject codexCollaborationMode(bool plan, const QString& model, const QString& effort);
} // namespace litecode::codex
