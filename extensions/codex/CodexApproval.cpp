#include "CodexApproval.h"
#include <QJsonArray>
#include <QStringList>

namespace litecode::codex {
CodexApprovalPrompt codexApprovalPrompt(const QString& method, const QJsonObject& params) {
    CodexApprovalPrompt result;
    const bool permissions = method == "item/permissions/requestApproval";
    const bool command = method == "item/commandExecution/requestApproval";
    result.category = permissions ? "Permissions" : command ? "Terminal" : "Files";
    result.title = permissions ? "Allow additional permissions for this turn?"
                   : command ? "Allow Codex to run this command?"
                             : "Allow Codex to change these files?";
    QStringList details;
    const auto reason = params.value("reason").toString().trimmed();
    if (!reason.isEmpty()) result.title = reason;
    if (command) {
        const auto text = params.value("command").toString();
        details.append(text.isEmpty() ? "Command details were not provided." : text);
        result.preview = text;
    }
    const auto cwd = params.value("cwd").toString();
    if (!cwd.isEmpty()) details.append("Working directory: " + cwd);
    const auto root = params.value("grantRoot").toString();
    if (!root.isEmpty()) details.append("Requested access: " + root);
    if (permissions) {
        const auto requested = params.value("permissions").toObject();
        const auto filesystem = requested.value("fileSystem").toObject();
        for (const auto& key : {QString("read"), QString("write")}) {
            QStringList paths;
            for (const auto& path : filesystem.value(key).toArray())
                if (path.isString()) paths.append(path.toString());
            if (!paths.isEmpty())
                details.append((key == "read" ? "Read access: " : "Write access: ") + paths.join('\n'));
        }
        if (requested.value("network").toObject().value("enabled").toBool())
            details.append("Network access requested.");
    }
    const auto add = [&](const QString& title, const QJsonValue& decision, bool close = false) {
        CodexApprovalChoice choice;
        choice.title = title;
        choice.isCloseAffordance = close;
        choice.decision = decision;
        result.choices.append(choice);
    };
    const auto offered = params.value("availableDecisions").toArray();
    if (permissions || offered.isEmpty() || offered.contains("accept"))
        add("Allow once", "accept");
    if (!permissions && offered.contains("acceptForSession"))
        add("Allow for this session", "acceptForSession");
    // Show the exact prefix before offering a broader execution policy.
    if (!permissions)
        for (const auto& decision : offered) {
            const auto amendment = decision.toObject().value("acceptWithExecpolicyAmendment")
                                       .toObject().value("execpolicy_amendment").toArray();
            QStringList prefix;
            bool valid = !amendment.isEmpty();
            for (const auto& part : amendment) {
                valid &= part.isString();
                prefix.append(part.toString());
            }
            if (valid) {
                details.append("Allow similar commands applies to this prefix:\n" + prefix.join(' '));
                add("Allow similar commands", decision);
                break;
            }
        }
    add("Decline", permissions || offered.isEmpty() || offered.contains("decline")
                       ? QJsonValue("decline") : QJsonValue("cancel"), true);
    result.detail = details.join("\n\n");
    if (result.preview.isEmpty()) result.preview = result.detail;
    return result;
}
QJsonObject codexApprovalResponse(const QString& method, const QJsonObject& params,
                                const std::optional<CodexApprovalChoice>& choice) {
    if (method == "item/permissions/requestApproval")
        return {{"permissions", choice && choice->decision == "accept"
                                    ? params.value("permissions").toObject() : QJsonObject{}},
                {"scope", "turn"}};
    const auto offered = params.value("availableDecisions").toArray();
    const QJsonValue declined = offered.isEmpty() || offered.contains("decline")
                                   ? QJsonValue("decline") : QJsonValue("cancel");
    const auto decision = choice && (offered.isEmpty() || offered.contains(choice->decision))
                              ? choice->decision : declined;
    return {{"decision", decision}};
}
} // namespace litecode::codex
