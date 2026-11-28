#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QThread>
#include <iostream>
#include <string>
namespace {
QString currentTurnId = "turn-fixture-0";
int turnSequence = 0;
void send(const QJsonObject& value) {
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    std::cout.write(bytes.constData(), bytes.size());
    std::cout << std::endl;
}
void notify(const QString& method, const QJsonObject& params) {
    send({{"method", method}, {"params", params}});
}
QJsonArray pagedTurns(int first, int last) {
    QJsonArray turns;
    for (int i = last; i >= first; --i)
        turns.append(QJsonObject{
            {"id", QString("paged-%1").arg(i)},
            {"status", "completed"},
            {"durationMs", 1000},
            {"items",
             QJsonArray{QJsonObject{{"id", QString("user-%1").arg(i)},
                                    {"type", "userMessage"},
                                    {"content", QJsonArray{QJsonObject{
                                                    {"type", "text"},
                                                    {"text", QString("Prompt %1").arg(i)}}}}},
                        QJsonObject{{"id", QString("answer-%1").arg(i)},
                                    {"type", "agentMessage"},
                                    {"phase", "final_answer"},
                                    {"text", QString("Paged answer %1. ").arg(i).repeated(20)}}}}});
    return turns;
}
void complete(const QString& status = "completed",
              const QString& text = "Hello from the native Codex extension fixture.") {
    notify("item/completed",
           {{"threadId", "thread-fixture"},
            {"turnId", currentTurnId},
            {"item", QJsonObject{{"id", "progress"},
                                 {"type", "agentMessage"},
                                 {"phase", "commentary"},
                                 {"text", "Checking the fixture before the final answer."}}}});
    for (int i = 0; i < 2; ++i)
        notify("item/completed",
               {{"threadId", "thread-fixture"},
                {"turnId", currentTurnId},
                {"item", QJsonObject{{"id", QString("command-%1").arg(i)},
                                     {"type", "commandExecution"},
                                     {"command", QString("echo command-%1").arg(i)},
                                     {"aggregatedOutput", "fixture command output"}}}});
    notify("item/started", {{"threadId", "thread-fixture"},
                            {"turnId", currentTurnId},
                            {"item", QJsonObject{{"id", "assistant"},
                                                 {"type", "agentMessage"},
                                                 {"phase", "final_answer"},
                                                 {"text", ""}}}});
    notify("item/agentMessage/delta", {{"threadId", "thread-fixture"},
                                       {"turnId", currentTurnId},
                                       {"itemId", "assistant"},
                                       {"delta", text}});
    notify("turn/completed", {{"threadId", "thread-fixture"},
                              {"turn", QJsonObject{{"id", currentTurnId},
                                                   {"status", status},
                                                   {"durationMs", 476000},
                                                   {"items", QJsonArray{}}}}});
}
} // namespace
int main() {
    QTemporaryFile observedRollout;
    if (!observedRollout.open())
        return 1;
    observedRollout.write(
        "{\"timestamp\":\"1970-01-01T00:01:40.000Z\",\"type\":\"event_msg\","
        "\"payload\":{\"type\":\"task_started\",\"turn_id\":\"observed-turn\"}}\n");
    observedRollout.flush();
    bool signedIn = true;
    QString accountType = "fixture";
    int ownedResumeAttempts = 0;
    int observedReads = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto message = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const auto id = message.value("id");
        const auto method = message.value("method").toString();
        const auto params = message.value("params").toObject();
        QJsonObject result;
        if (id == QJsonValue("approval-fixture") && message.contains("result")) {
            complete("completed",
                     "Approval decision: " +
                         message.value("result").toObject().value("decision").toString());
            continue;
        }
        if (method.isEmpty() || id.isUndefined())
            continue;
        // Keep opening observable so navigation/loading is verified before
        // the result arrives, including Back during an in-flight request.
        if (method == "thread/resume")
            QThread::msleep(300);
        if (method == "thread/resume" && params.value("threadId") == "thread-slow-open")
            QThread::msleep(600);
        if (method == "thread/resume" && params.value("threadId") == "thread-missing") {
            send({{"id", id}, {"error", QJsonObject{{"message", "Saved chat was not found"}}}});
            continue;
        }
        if (method == "thread/resume" && params.value("threadId") == "thread-observed") {
            send({{"id", id},
                  {"error", QJsonObject{{"message", "thread already has an active writer"}}}});
            continue;
        }
        if (method == "thread/resume" && params.value("threadId") == "thread-owned" &&
            ++ownedResumeAttempts == 1) {
            send({{"id", id},
                  {"error",
                   QJsonObject{{"message", "thread thread-owned already has an active writer"}}}});
            continue;
        }
        if (method == "model/list")
            result = {{"data",
                       QJsonArray{QJsonObject{
                           {"id", "fixture"},
                           {"model", "fixture"},
                           {"displayName", "Fixture model"},
                           {"defaultReasoningEffort", "medium"},
                           {"serviceTiers", QJsonArray{QJsonObject{{"id", "priority"},
                                                                   {"name", "Fast"},
                                                                   {"description", "1.5x speed"}}}},
                           {"supportedReasoningEfforts",
                            QJsonArray{QJsonObject{{"reasoningEffort", "low"}},
                                       QJsonObject{{"reasoningEffort", "medium"}},
                                       QJsonObject{{"reasoningEffort", "high"}}}},
                           {"isDefault", true}}}}};
        else if (method == "account/read")
            result = {{"account", signedIn ? QJsonValue(QJsonObject{{"type", accountType}})
                                           : QJsonValue(QJsonValue::Null)},
                      {"requiresOpenaiAuth", true}};
        else if (method == "account/logout")
            signedIn = false;
        else if (method == "account/login/start") {
            const auto type = params.value("type").toString();
            if (type == "apiKey") {
                if (params.value("apiKey") != "fixture-api-key") {
                    send({{"id", id}, {"error", QJsonObject{{"message", "Invalid fixture key"}}}});
                    continue;
                }
                signedIn = true;
                accountType = "apiKey";
                result = {{"type", "apiKey"}};
            } else if (type == "chatgptDeviceCode")
                result = {{"type", type},
                          {"loginId", "fixture-device-login"},
                          {"userCode", "FIXTURE-CODE"},
                          {"verificationUrl", "https://example.invalid/device"}};
            else
                result = {{"type", type},
                          {"loginId", "fixture-browser-login"},
                          {"authUrl", "https://example.invalid/login"}};
        } else if (method == "thread/goal/get")
            result = {{"goal", QJsonObject{{"status", "blocked"},
                                           {"objective",
                                            QString("Long saved goal objective. ").repeated(40)}}}};
        else if (method == "thread/turns/list" && params.value("threadId") == "thread-fixture-3") {
            const bool oldest = params.value("cursor") == "older-fixture-page-2";
            result = {
                {"data", oldest ? pagedTurns(0, 7) : pagedTurns(8, 15)},
                {"nextCursor", oldest ? QJsonValue::Null : QJsonValue("older-fixture-page-2")}};
        } else if (method == "thread/resume" && params.value("threadId") == "thread-fixture-3")
            result = {
                {"thread", QJsonObject{{"id", "thread-fixture-3"}, {"name", "Paged fixture chat"}}},
                {"initialTurnsPage",
                 QJsonObject{{"data", pagedTurns(16, 23)}, {"nextCursor", "older-fixture-page"}}}};
        else if (method == "thread/turns/list" && params.value("sortDirection") == "asc") {
            const bool second = params.value("cursor") == "markdown-next";
            QJsonObject item =
                second
                    ? QJsonObject{{"type", "agentMessage"},
                                  {"phase", "final_answer"},
                                  {"text", "Hello from the native Codex extension fixture."}}
                    : QJsonObject{{"type", "userMessage"},
                                  {"content",
                                   QJsonArray{QJsonObject{
                                       {"type", "text"},
                                       {"text", "fixture changes request delayed-acceptance"}}}}};
            result = {
                {"data", QJsonArray{QJsonObject{{"id", second ? "export-second" : "export-first"},
                                                {"status", "completed"},
                                                {"items", QJsonArray{item}}}}},
                {"nextCursor",
                 second ? QJsonValue(QJsonValue::Null) : QJsonValue("markdown-next")}};
        } else if (method == "thread/turns/list" && params.value("threadId") == "thread-observed") {
            const bool finished = ++observedReads > 1;
            QJsonArray items{QJsonObject{
                {"id", "observed-user"},
                {"type", "userMessage"},
                {"content", QJsonArray{QJsonObject{
                                {"type", "text"},
                                {"text", "User message before the other session completes"}}}}}};
            if (finished)
                items.append(QJsonObject{{"id", "observed-answer"},
                                         {"type", "agentMessage"},
                                         {"phase", "final_answer"},
                                         {"text", "The answer completed in the owning session."}});
            result = {{"data", QJsonArray{QJsonObject{
                                   {"id", "observed-turn"},
                                   {"status", finished ? "completed" : "interrupted"},
                                   {"itemsView", "full"},
                                   {"startedAt", 100},
                                   {"completedAt",
                                    finished ? QJsonValue(165) : QJsonValue(QJsonValue::Null)},
                                   {"durationMs", QJsonValue(QJsonValue::Null)},
                                   {"items", items}}}},
                      {"nextCursor", QJsonValue(QJsonValue::Null)}};
        } else if (method == "thread/turns/list")
            result = {
                {"data", QJsonArray{QJsonObject{
                             {"id", "saved-turn"},
                             {"status", "inProgress"},
                             {"items", QJsonArray{QJsonObject{
                                           {"id", "saved-answer"},
                                           {"type", "agentMessage"},
                                           {"text", "Saved answer from another session."}}}}}}},
                {"nextCursor", QJsonValue::Null}};
        else if (method == "thread/start" || method == "thread/resume" || method == "thread/read")
            result = {
                {"thread", QJsonObject{{"id", params.value("threadId").toString("thread-fixture")},
                                       {"name", "Fixture chat"},
                                       {"path", observedRollout.fileName()},
                                       {"turns", QJsonArray{}}}}};
        else if (method == "thread/list") {
            if (params.contains("cwd")) {
                send({{"id", id},
                      {"error", QJsonObject{{"message", "All chats must not filter by folder"}}}});
                continue;
            }
            QJsonArray threads;
            const bool second = params.value("cursor") == "fixture-page-2";
            for (int i = second ? 4 : 0; i < (second ? 6 : 5); ++i)
                threads.append(
                    QJsonObject{{"id", i == 0   ? "thread-fixture"
                                       : i == 1 ? "thread-owned"
                                       : i == 2 ? "thread-missing"
                                                : QString("thread-fixture-%1").arg(i)},
                                {"name", QString("Fixture chat %1").arg(i == 5 ? 0 : i)}});
            // Reproduce duplicate rollout entries within a page and overlap
            // across pages, while retaining a different ID with the same title.
            threads.append(threads.first());
            result = {{"data", threads},
                      {"nextCursor", second ? QJsonValue::Null : QJsonValue("fixture-page-2")}};
        } else if (method == "turn/steer" &&
                   params.value("input").toArray().first().toObject().value("text") ==
                       "fixture rejected follow-up") {
            send({{"id", id}, {"error", QJsonObject{{"message", "fixture rejected follow-up"}}}});
            continue;
        } else if (method == "turn/start") {
            currentTurnId = "turn-fixture-" + QString::number(++turnSequence);
            result = {{"turn", QJsonObject{{"id", currentTurnId}, {"status", "inProgress"}}}};
        }
        if (method == "turn/start" &&
            params.value("input").toArray().first().toObject().value("text").toString().contains(
                "delayed-acceptance"))
            QThread::msleep(500); // Exercise typing while the submission is in flight.
        send({{"id", id}, {"result", result}});
        if (method == "turn/start") {
            notify("turn/started",
                   {{"threadId", "thread-fixture"},
                    {"turn", QJsonObject{{"id", currentTurnId}, {"status", "inProgress"}}}});
            const auto prompt =
                params.value("input").toArray().first().toObject().value("text").toString();
            notify("item/started",
                   {{"threadId", "thread-fixture"},
                    {"turnId", currentTurnId},
                    {"item", QJsonObject{{"id", "user"},
                                         {"type", "userMessage"},
                                         {"content", QJsonArray{QJsonObject{{"type", "text"},
                                                                            {"text", prompt}}}}}}});
            if (prompt.startsWith("policy-check:")) {
                const auto mode = params.value("collaborationMode").toObject();
                const auto settings = mode.value("settings").toObject();
                const auto expected = prompt.section(':', 1, 1);
                const bool full = expected == "full";
                const bool autoReview = expected == "auto";
                const bool valid =
                    mode.value("mode") == (prompt.endsWith(":plan") ? "plan" : "default") &&
                    settings.value("model") == "fixture" &&
                    settings.value("reasoning_effort") == "medium" &&
                    settings.contains("developer_instructions") &&
                    settings.value("developer_instructions").isNull() &&
                    params.value("approvalPolicy") == (full ? "never" : "on-request") &&
                    params.value("approvalsReviewer") ==
                        (autoReview ? "guardian_subagent" : "user") &&
                    params.value("sandboxPolicy").toObject().value("type") ==
                        (full ? "dangerFullAccess" : "workspaceWrite");
                complete("completed", valid ? "policy-accepted" : "policy-incorrect");
                continue;
            }
            if (prompt == "fast-tier-required" || prompt == "standard-tier-required") {
                const bool valid =
                    prompt == "fast-tier-required"
                        ? params.value("serviceTier") == "priority"
                        : params.contains("serviceTier") && params.value("serviceTier").isNull();
                complete("completed", valid ? "tier-accepted" : "tier-incorrect");
                continue;
            }
            if (prompt.contains("approval"))
                send({{"id", "approval-fixture"},
                      {"method", "item/commandExecution/requestApproval"},
                      {"params",
                       QJsonObject{{"threadId", "thread-fixture"},
                                   {"turnId", currentTurnId},
                                   {"command", "fixture-no-real-command"},
                                   {"availableDecisions", QJsonArray{"accept", "decline"}}}}});
            else if (prompt.contains("changes")) {
                QString diff = "diff --git a/example.py b/example.py\n--- a/example.py\n+++ "
                               "b/example.py\n@@ -1 +1 @@\n-old\n+new\n";
                if (prompt.startsWith("fixture changes edited ")) {
                    const auto path = prompt.mid(QString("fixture changes edited ").size());
                    diff.replace("a/example.py", "a/" + path);
                    diff.replace("b/example.py", "b/" + path);
                    diff.replace("\n", "\r\n");
                    diff += "diff --git a/unavailable.py b/unavailable.py\n"
                            "--- a/unavailable.py\n+++ b/unavailable.py\n"
                            "@@ -1 +1 @@\n-old neighbor\n+new neighbor\n";
                }
                notify("turn/diff/updated",
                       {{"threadId", "thread-fixture"}, {"turnId", currentTurnId}, {"diff", diff}});
                complete();
            } else if (!prompt.contains("wait"))
                complete();
        } else if (method == "turn/interrupt")
            complete("interrupted", "Turn interrupted");
    }
}
