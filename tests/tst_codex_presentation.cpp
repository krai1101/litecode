#include "CodexAttachmentStore.h"
#include "CodexThreadLink.h"
#include "ConversationMarkdown.h"
#include "CodexHistoryWindow.h"
#include "CodexTurnPresentation.h"
#include "CodexViewIcons.h"
#include "TurnHistory.h"
#include "TurnUndo.h"
#include "ObservedTurnLifecycle.h"
#include "ToolCallDisplay.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode::codex;
namespace {
litecode::extensions::NativeControl workDetails(const litecode::extensions::NativeControl& work) {
    for (const auto& child : work.children)
        if (child.id.endsWith(".workDetails")) return child;
    return {};
}
}
class CodexPresentationTests final : public QObject {
    Q_OBJECT
  private slots:
    void signedOutViewHasDedicatedAuthenticationAndNoConversation() {
        using namespace litecode::extensions;
        CodexViewState state;
        state.accountKnown = true;
        state.ready = true;
        QVERIFY(codexSignedOut(state));
        const auto find = [](const NativeControl& root, const QString& id) -> const NativeControl* {
            std::function<const NativeControl*(const NativeControl&)> visit;
            visit = [&](const NativeControl& node) -> const NativeControl* {
                if (node.id == id) return &node;
                for (const auto& child : node.children)
                    if (const auto* found = visit(child)) return found;
                return nullptr;
            };
            return visit(root);
        };
        auto root = codexViewLayout(state);
        QVERIFY(find(root, "codex.auth.chatgpt"));
        QVERIFY(find(root, "codex.auth.apiKey"));
        QVERIFY(!find(root, "codex.prompt"));
        state.apiKeyEntry = true;
        root = codexViewLayout(state);
        const auto* key = find(root, "codex.auth.key");
        QVERIFY(key);
        QVERIFY(key->password);
        QVERIFY(key->text.isEmpty());
        QVERIFY(!find(root, "codex.auth.keySubmit")->enabled);
        state.requiresAuth = false;
        QVERIFY(!codexSignedOut(state)); // Custom providers may need no OpenAI account.
        state.signedIn = true;
        state.historyLoaded = true;
        state.confirmingLogout = true;
        root = codexViewLayout(state);
        QVERIFY(find(root, "codex.auth.logoutDialog"));
        QVERIFY(find(root, "codex.auth.logoutConfirm"));
    }
    void approvalDetailsAreReadableAndDecisionsStayExact() {
        const QString method = "item/commandExecution/requestApproval";
        const QJsonObject amendment{{"acceptWithExecpolicyAmendment",
            QJsonObject{{"execpolicy_amendment", QJsonArray{"cmake", "--build"}}}}};
        const QJsonObject params{{"reason", "Build the updated UI."},
            {"command", "cmake --build build/release"}, {"cwd", "/workspace"},
            {"availableDecisions", QJsonArray{"accept", amendment, "cancel"}}};
        const auto prompt = codexApprovalPrompt(method, params);
        QCOMPARE(prompt.title, QString("Build the updated UI."));
        QCOMPARE(prompt.category, QString("Terminal"));
        QVERIFY(prompt.detail.contains("cmake --build build/release"));
        QVERIFY(prompt.detail.contains("Working directory: /workspace"));
        QVERIFY(!prompt.detail.contains("availableDecisions"));
        QCOMPARE(prompt.choices.size(), 3);
        QCOMPARE(codexApprovalResponse(method, params, prompt.choices[1]).value("decision"), amendment);
        QCOMPARE(codexApprovalResponse(method, params, std::nullopt).value("decision"), QJsonValue("cancel"));
        CodexViewState state;
        state.approval = prompt;
        state.approvalKey = "codex.approval.1";
        const auto card = codexApprovalLayout(state);
        QVERIFY(card.visible);
        QCOMPARE(card.appearance, litecode::extensions::NativeControlAppearance::Composer);
        state.approvalExpanded = true;
        const auto expanded = codexApprovalLayout(state);
        QCOMPARE(expanded.children[2].kind, litecode::extensions::NativeControlKind::TextEdit);
        QVERIFY(expanded.children[2].readOnly);
        QCOMPARE(expanded.children[2].text, prompt.detail);
    }
    void turnErrorsRemainInlineBeforeFileChanges() {
        CodexViewState state;
        CodexTurnView turn;
        turn.key = "limited";
        turn.status = "failed";
        turn.error = "You've hit your usage limit. Upgrade your plan or add credits to continue, or try again later.";
        turn.files.append({"example.py", 1, 0});
        state.turns.append(turn);
        const auto layout = codexConversationLayout(state);
        const auto& response = layout.children.first().children[2].children[2];
        QCOMPARE(response.id, QString("codex.turn.limited.responseGroup"));
        QCOMPARE(response.children[1].id, QString("codex.turn.limited.errorNotice"));
        QVERIFY(response.children[1].visible);
        QCOMPARE(response.children[1].children[1].text, turn.error);
        QCOMPARE(response.children[1].children[1].appearance,
                 litecode::extensions::NativeControlAppearance::Muted);
        QVERIFY(!response.children[1].children[0].enabled);
        QVERIFY(response.children[2].id.endsWith(".changes"));
    }
    void crlfPatchesReconstructFullSnapshotsWithoutChangingFiles() {
        QTemporaryDir workspace;
        QFile file(workspace.filePath("example.py"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("context\r\nnew\r\n");
        file.close();
        const QString patch = "diff --git a/example.py b/example.py\n"
                              "--- a/example.py\n+++ b/example.py\n"
                              "@@ -1,2 +1,2 @@\n context\r\n-old\r\n+new\r\n";
        TurnUndo review(patch, workspace.path());
        QCOMPARE(review.snapshots().size(), 1);
        QCOMPARE(review.snapshots().first().before, QByteArray("context\r\nold\r\n"));
        QCOMPARE(review.snapshots().first().after, QByteArray("context\r\nnew\r\n"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("context\r\nnew\r\n"));
    }
    void externalRunningSnapshotsUseDurableLifecycleRatherThanInterruptedFallback() {
        QTemporaryDir directory;
        const auto path = directory.filePath("rollout.jsonl");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto event = [](const char* type, const char* timestamp) {
            return QJsonDocument(QJsonObject{
                {"timestamp", timestamp}, {"type", "event_msg"},
                {"payload", QJsonObject{{"type", type}, {"turn_id", "observed"}}}})
                .toJson(QJsonDocument::Compact) + '\n';
        };
        const auto started = QDateTime::currentDateTimeUtc().addSecs(-89);
        file.write(event("task_started", started.toString(Qt::ISODateWithMs).toUtf8().constData()));
        // Oversized image records and an incomplete write must not hide the start
        // or allocate/decode the full transcript on the UI thread.
        file.write(QByteArray(200000, 'x') + '\n');
        file.write("{\"unfinished\":");
        file.flush();
        const auto lifecycle = readObservedTurnLifecycle(path, "observed");
        QCOMPARE(lifecycle.status, "inProgress");
        QCOMPARE(lifecycle.startedMs, started.toMSecsSinceEpoch());
        QJsonObject snapshot{{"id", "observed"}, {"status", "interrupted"},
                              {"startedAt", QJsonValue::Null}, {"durationMs", QJsonValue::Null},
                              {"items", QJsonArray{QJsonObject{{"type", "agentMessage"},
                                  {"phase", "commentary"}, {"text", "Checking the code."}}}}};
        TurnState turn;
        readTurnHistory(reconcileObservedTurn(snapshot, lifecycle), turn);
        CodexViewState state;
        projectCodexHistoryWindow(state, {turn}, false);
        const auto running = state.turns.first();
        QVERIFY(running.workLabel.startsWith("Working for 1m "));
        const auto work = codexWorkLayout(running, state);
        QCOMPARE(work.children.first().kind, litecode::extensions::NativeControlKind::Label);
        QVERIFY(!work.children.first().text.contains("chevron"));
        QVERIFY(codexWorkExpanded(running, state));
        // A real abort with missing API timing is still terminal, never guessed live.
        file.write("null}\n");
        file.write(event("turn_aborted", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8().constData()));
        file.flush();
        const auto aborted = readObservedTurnLifecycle(path, "observed", lifecycle);
        QCOMPARE(aborted.status, "interrupted");
        snapshot.insert("startedAt", started.toSecsSinceEpoch());
        readTurnHistory(reconcileObservedTurn(snapshot, aborted), turn);
        QVERIFY(turn.durationMs >= 89000);
        state.turns.clear();
        projectCodexHistoryWindow(state, {turn}, false);
        QVERIFY(state.turns.first().workLabel.startsWith("Worked for "));
        QCOMPARE(codexWorkLayout(state.turns.first(), state).children.first().kind,
                 litecode::extensions::NativeControlKind::Button);
        snapshot.insert("status", "completed");
        snapshot.insert("durationMs", 14000);
        QCOMPARE(reconcileObservedTurn(snapshot, lifecycle), snapshot);
        QCOMPARE(reconcileObservedTurn(snapshot, {}), snapshot);
    }
    void observedToolCallsRemainRunningUntilTheirMatchingOutput() {
        QTemporaryDir directory;
        QFile file(directory.filePath("rollout.jsonl"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto append = [&](const QJsonObject& record) {
            file.write(QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n');
            QVERIFY(file.flush());
        };
        const auto response = [&](const QJsonObject& payload) {
            append(QJsonObject{{"type", "response_item"}, {"payload", payload}});
        };
        append(QJsonObject{{"type", "event_msg"}, {"timestamp", "2026-11-08T07:00:00.000Z"},
            {"payload", QJsonObject{{"type", "task_started"}, {"turn_id", "turn"}}}});
        response(QJsonObject{{"type", "custom_tool_call"}, {"call_id", "done"},
                             {"name", "exec"}, {"status", "completed"}});
        response(QJsonObject{{"type", "function_call"}, {"call_id", "command"},
            {"name", "exec_command"},
            {"arguments", R"({"cmd":"python probe.py"})"}});
        // Completing an unrelated tool must not clear the running command.
        response(QJsonObject{{"type", "custom_tool_call_output"}, {"call_id", "done"}});
        auto observed = readObservedTurnLifecycle(file.fileName(), "turn");
        QCOMPARE(observed.activeTools.size(), 1);
        QJsonObject apiTurn{{"id", "turn"}, {"status", "interrupted"}};
        TurnState turn;
        readTurnHistory(reconcileObservedTurn(apiTurn, observed), turn);
        CodexViewState state;
        projectCodexHistoryWindow(state, {turn}, false);
        QVERIFY(!state.turns.first().thinking);
        QCOMPARE(codexToolLabel(state.turns.first().progress.first()),
                 QString("$(terminal) Running python probe.py"));
        response(QJsonObject{{"type", "function_call_output"}, {"call_id", "command"}});
        observed = readObservedTurnLifecycle(file.fileName(), "turn", observed);
        QVERIFY(observed.activeTools.isEmpty());
        response(QJsonObject{{"type", "custom_tool_call"}, {"call_id", "custom"},
                             {"name", "exec"}, {"input", "await tools.someTool()"},
                             {"status", "completed"}});
        observed = readObservedTurnLifecycle(file.fileName(), "turn", observed);
        QCOMPARE(observed.activeTools.size(), 1);
        readTurnHistory(reconcileObservedTurn(apiTurn, observed), turn);
        state.turns.clear();
        projectCodexHistoryWindow(state, {turn}, false);
        QVERIFY(!state.turns.first().thinking);
        QCOMPARE(state.turns.first().progress.first().text, QString("Running exec"));
        // Image/tool outputs can exceed the bounded reverse-reader chunk size.
        // Their small completion envelope must still retire the pending call.
        // Match app-server's serialized envelope: type/call_id precede output.
        file.write("{\"type\":\"response_item\",\"payload\":{\"type\":\"custom_tool_call_output\",\"call_id\":\"custom\",\"output\":\"");
        file.write(QByteArray(300000, 'x'));
        file.write("\"}}\n");
        file.flush();
        observed = readObservedTurnLifecycle(file.fileName(), "turn", observed);
        QVERIFY(observed.activeTools.isEmpty());
        // An unfinished large output is not a completed call yet.
        response(QJsonObject{{"type", "function_call"}, {"call_id", "image"},
            {"name", "view_image"}, {"arguments", R"({"path":"/tmp/image.png"})"}});
        file.write("{\"type\":\"response_item\",\"payload\":{\"type\":\"function_call_output\",\"call_id\":\"image\",\"output\":\"");
        file.write(QByteArray(200000, 'x'));
        file.write("\"}}");
        file.flush();
        observed = readObservedTurnLifecycle(file.fileName(), "turn", observed);
        QCOMPARE(observed.activeTools.size(), 1);
        QCOMPARE(observed.activeTools.first().toObject().value("type").toString(),
                 QString("imageView"));
        file.write("\n");
        file.flush();
        observed = readObservedTurnLifecycle(file.fileName(), "turn", observed);
        QVERIFY(observed.activeTools.isEmpty());

        append(QJsonObject{{"type", "event_msg"}, {"timestamp", "2026-11-08T07:00:10.000Z"},
            {"payload", QJsonObject{{"type", "task_complete"}, {"turn_id", "turn"}}}});
        observed = readObservedTurnLifecycle(file.fileName(), "turn", observed);
        QCOMPARE(observed.status, QString("completed"));
        QVERIFY(observed.activeTools.isEmpty());
    }
    void wrappedToolCallsExposeLiteralCommandsAndImagesWithoutExecutingCode() {
        const auto display = [](const QString& code) {
            return toolCallDisplay(QJsonObject{{"type", "custom_tool_call"},
                {"name", "exec"}, {"input", code}});
        };
        auto item = display(R"(text(await tools.exec_command({cmd:"python probe.py\npytest -q", workdir:"/tmp"}));)");
        QCOMPARE(item.value("type").toString(), QString("commandExecution"));
        QCOMPARE(item.value("command").toString(), QString("python probe.py\npytest -q"));
        item = display(R"(image((await tools.view_image({path:"/tmp/capture.png"})).image_url);)");
        QCOMPARE(item.value("type").toString(), QString("imageView"));
        QCOMPARE(item.value("path").toString(), QString("/tmp/capture.png"));
        // Quoted examples, comments and dynamically computed values are not calls.
        QCOMPARE(display(R"js(const example = "tools.exec_command({cmd:fake})";)js")
                     .value("type").toString(), QString("dynamicToolCall"));
        QCOMPARE(display(R"(/* tools.exec_command({cmd:"fake"}) */ tools.exec_command({cmd:variable}))")
                     .value("type").toString(), QString("dynamicToolCall"));
        QCOMPARE(display(R"(tools.exec_command({cmd:"prefix" + variable}))")
                     .value("type").toString(), QString("dynamicToolCall"));
        QCOMPARE(display(R"(tools.exec_command({nested:{cmd:"fake"},cmd:"real"}))")
                     .value("command").toString(), QString("real"));
    }
    void commandsAndImageActivityPreserveOrderAndTheActualRunningCommand() {
        TurnState turn;
        readTurnHistory(QJsonObject{{"id", "turn"}, {"status", "inProgress"},
            {"items", QJsonArray{
                QJsonObject{{"id", "first"}, {"type", "commandExecution"},
                    {"status", "completed"}, {"command", "echo first"}},
                QJsonObject{{"id", "image"}, {"type", "imageView"}, {"path", "/tmp/capture.png"}},
                QJsonObject{{"id", "second"}, {"type", "commandExecution"},
                    {"status", "inProgress"}, {"command", "powershell.exe -Command quoted-script"},
                    {"commandActions", QJsonArray{QJsonObject{{"type", "unknown"},
                        {"command", "python probe.py\npytest -q"}}}}}}}}, turn);
        CodexTurnView view;
        view.key = "activities";
        int budget = 16384;
        populateTurnPresentation(turn, view, budget, true);
        QVERIFY(!view.thinking);
        QCOMPARE(view.progress.size(), 3);
        QCOMPARE(view.progress[1].text, QString("Viewed an image"));
        const auto details = workDetails(codexWorkLayout(view, {}));
        QCOMPARE(details.children.size(), 3);
        QCOMPARE(details.children[0].children.first().text, QString("$(terminal) Ran commands"));
        QCOMPARE(details.children[1].text, QString("$(file-media) Viewed an image"));
        QCOMPARE(details.children[2].text, QString("$(terminal) Running python probe.py pytest -q"));
        turn.activeTools.clear();
        turn.items[1].insert("status", "inProgress");
        turn.items[2].insert("status", "completed");
        view = {};
        budget = 16384;
        populateTurnPresentation(turn, view, budget, true);
        QVERIFY(!view.thinking);
        QCOMPARE(view.progress[1].text, QString("Viewing an image"));
    }
    void threadLinksRoundTripAndRejectOtherRoutes() {
        const auto link = codexThreadLink("litecode", "litecode.codex", "thread-123");
        QCOMPARE(link.toString(), QString("litecode://litecode.codex/thread/thread-123"));
        QString opened;
        CodexThreadLinkHandler handler("litecode", "litecode.codex",
                                       [&](const QString& id) { opened = id; });
        handler.handleUri(link);
        QCOMPARE(opened, QString("thread-123"));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
            handler.handleUri(QUrl("litecode://other.extension/thread/thread-123")));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
            handler.handleUri(QUrl("litecode://litecode.codex/thread/thread-123?command=run")));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
            handler.handleUri(QUrl("litecode://litecode.codex/thread/a/b")));
    }
    void markdownExportRetainsRolesAndMarkdownFormatting() {
        TurnState turn;
        turn.items = {QJsonObject{{"type", "userMessage"},
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", "Explain this code."}}}}},
            QJsonObject{{"type", "agentMessage"}, {"phase", "final_answer"},
                {"text", "**Example**\n\n```python\nprint(42)\n```"}}};
        const auto markdown = codexTurnMarkdown(turn);
        QVERIFY(markdown.startsWith("## User\n\nExplain this code.\n\n"));
        QVERIFY(markdown.contains("## Assistant\n\n**Example**\n\n```python\nprint(42)\n```"));
    }
    void homeShowsRecentDatesAndStartsWithAFreshComposer() {
        CodexViewState state;
        state.ready = true;
        state.historyLoaded = true;
        state.turns.append(CodexTurnView{}); // Returning home retains the open chat cache.
        const auto now = QDateTime::currentSecsSinceEpoch();
        for (int i = 0; i < 4; ++i)
            state.threads.append(QJsonObject{
                {"id", QString::number(i)}, {"name", "Chat"}, {"updatedAt", now - 4 * 86400}});
        const auto find = [](auto&& self, const litecode::extensions::NativeControl& node,
                             const QString& id) -> const litecode::extensions::NativeControl* {
            if (node.id == id)
                return &node;
            for (const auto& child : node.children)
                if (const auto* result = self(self, child, id))
                    return result;
            return nullptr;
        };
        const auto layout = codexViewLayout(state);
        QCOMPARE(find(find, layout, "codex.prompt")->placeholder, "Do anything");
        QCOMPARE(find(find, layout, "codex.recentThreads")->items.size(), 3);
        QCOMPARE(find(find, layout, "codex.recentThreads")->items.first(), "Chat\t4d");
        QCOMPARE(find(find, layout, "codex.viewAllHistory")->text, "View all (4)");
        QVERIFY(find(find, layout, "codex.homeMark"));
        QCOMPARE(find(find, layout, "codex.settings")->toolTip, "Settings");
        state.historyCursor = "next";
        const auto paged = codexHistoryLayout(state);
        QCOMPARE(find(find, paged, "codex.viewAllHistory")->text, "View all");
    }
    void contextMeterIsDeterminateAndUsesReportedTokenCounts() {
        CodexViewState state;
        state.home = false;
        state.contextUsed = 207000;
        state.contextLimit = 475000;
        const auto find = [](auto&& self, const litecode::extensions::NativeControl& node,
                             const QString& id) -> const litecode::extensions::NativeControl* {
            if (node.id == id)
                return &node;
            for (const auto& child : node.children)
                if (const auto* result = self(self, child, id))
                    return result;
            return nullptr;
        };
        auto layout = codexComposerLayout(state);
        auto* meter = find(find, layout, "codex.usage");
        QVERIFY(meter && meter->visible);
        QCOMPARE(meter->kind, litecode::extensions::NativeControlKind::Progress);
        QCOMPARE(meter->value, 44);
        QCOMPARE(meter->text, "Context window:\n44% used (56% left)\n207k / 475k tokens used");
        QVERIFY(!find(find, layout, "codex.generating"));
        state.busy = true;
        layout = codexComposerLayout(state);
        QCOMPARE(find(find, layout, "codex.usage")->value, 44);
        state.contextLimit = 0;
        layout = codexComposerLayout(state);
        QVERIFY(!find(find, layout, "codex.usage")->visible);
    }
    void changedFilesHaveRelativeNamesAndRowHover() {
        CodexViewState state;
        state.workspace = QDir::currentPath();
        CodexTurnView turn;
        turn.key = "fixture";
        turn.files.append({QDir::current().filePath("src/example.cpp"), 4, 2});
        const auto card = codexChangesLayout(turn, state, true);
        QCOMPARE(card.appearance, litecode::extensions::NativeControlAppearance::Surface);
        const auto& row = card.children[2];
        QCOMPARE(row.appearance, litecode::extensions::NativeControlAppearance::HoverGroup);
        QCOMPARE(row.children.first().text, "src/example.cpp");
        QVERIFY(row.children.first().accessibleName.contains(turn.files.first().path));
    }
    void conversationAndHistoryShareOnePaddedContentContainer() {
        CodexViewState state;
        state.historyLoaded = true;
        const auto content = [](const auto& root) {
            for (const auto& child : root.children)
                if (child.id == "codex.content")
                    return child;
            return litecode::extensions::NativeControl{};
        };
        const auto homeLayout = codexViewLayout(state);
        QVERIFY(homeLayout.contentsMargins);
        QCOMPARE(homeLayout.contentsMargins->bottom(), 20);
        const auto home = content(homeLayout);
        QCOMPARE(home.id, "codex.content");
        QCOMPARE(home.margin, 0);
        QVERIFY(home.contentsMargins);
        QCOMPARE(*home.contentsMargins, QMargins(CodexContentInset, 0, 0, 0));
        QStringList children;
        for (const auto& child : home.children)
            children.append(child.id);
        QVERIFY(children.contains("codex.toolbar"));
        QVERIFY(children.contains("codex.footer"));
        QVERIFY(children.contains("codex.historyPanel"));
        QVERIFY(children.contains("codex.conversation"));
        state.home = false;
        const auto conversation = content(codexViewLayout(state));
        QCOMPARE(conversation.margin, home.margin);
        QCOMPARE(conversation.contentsMargins, home.contentsMargins);
        state.history = true;
        QCOMPARE(content(codexViewLayout(state)).margin, home.margin);
    }
    void thinkingTransitionsThroughToolsAndStreamingToACompletedAnswer() {
        TurnState turn;
        turn.status = "inProgress";
        turn.startedMs = QDateTime::currentMSecsSinceEpoch() - 43000;
        const auto present = [&](bool active) {
            CodexTurnView view;
            view.key = "transition";
            int budget = 128 * 1024;
            populateTurnPresentation(turn, view, budget, active);
            return view;
        };
        auto view = present(true);
        QVERIFY(view.thinking);
        QVERIFY(view.workLabel.startsWith("Working for "));
        auto work = codexWorkLayout(view, {});
        QVERIFY(work.children.last().visible);
        QCOMPARE(work.children.last().children.first().text, "Thinking");
        QCOMPARE(work.children.last().children.first().appearance,
                 litecode::extensions::NativeControlAppearance::Shimmer);
        turn.activeTools.insert("command");
        QVERIFY(!present(true).thinking);
        turn.activeTools.clear();
        QVERIFY(present(true).thinking);
        turn.items.append(QJsonObject{{"id", "progress"},
                                      {"type", "agentMessage"},
                                      {"phase", "commentary"},
                                      {"text", "Checking the code."}});
        turn.items.append(QJsonObject{
            {"id", "answer"}, {"type", "agentMessage"}, {"phase", "final_answer"}, {"text", ""}});
        view = present(true);
        QVERIFY(view.thinking);
        QVERIFY(view.response.isEmpty());
        turn.items.last().insert("text", "The final answer.");
        view = present(true);
        QVERIFY(!view.thinking);
        QVERIFY(view.responseStreaming);
        QCOMPARE(view.progress.size(), 1);
        const auto finalText = view.response;
        turn.status = "completed";
        turn.durationMs = 43000;
        view = present(false);
        QVERIFY(!view.thinking && !view.responseStreaming);
        QCOMPARE(view.response, finalText);
        QCOMPARE(view.workLabel, "Worked for 43s");
        QVERIFY(!codexWorkExpanded(view, {}));
    }
    void runningCommandsStayVisibleAndSuppressThinkingUntilCompletion() {
        TurnState turn;
        readTurnHistory(QJsonObject{{"id", "turn"}, {"status", "inProgress"},
            {"items", QJsonArray{QJsonObject{{"id", "command"}, {"type", "commandExecution"},
                {"status", "inProgress"}, {"command", "python build/probe.py"}}}}}, turn);
        const auto present = [&] {
            CodexTurnView view;
            view.key = "running-command";
            int budget = 128 * 1024;
            populateTurnPresentation(turn, view, budget, true);
            return view;
        };
        auto view = present();
        QVERIFY(!view.thinking);
        QVERIFY(view.progress.first().running);
        QCOMPARE(codexToolLabel(view.progress.first()),
                 QString("$(terminal) Running python build/probe.py"));
        auto details = workDetails(codexWorkLayout(view, {}));
        QCOMPARE(details.children.size(), 1);
        QCOMPARE(details.children.first().text,
                 QString("$(terminal) Running python build/probe.py"));
        QVERIFY(codexProgressVisible(view, {}, 0));
        // The notification path can identify activity before status is populated.
        turn.items.first().remove("status");
        QVERIFY(present().progress.first().running);
        turn.activeTools.clear();
        turn.items.first().insert("status", "completed");
        view = present();
        QVERIFY(view.thinking);
        QVERIFY(!view.progress.first().running);
        details = workDetails(codexWorkLayout(view, {}));
        QCOMPARE(details.children.first().children.first().text,
                 QString("$(terminal) Ran commands"));
    }
    void compactionActivityUsesItemLifecycleNotTheOverallTurnStatus() {
        TurnState turn;
        turn.status = "inProgress";
        turn.items = {QJsonObject{{"id", "compaction"}, {"type", "contextCompaction"}}};
        turn.activeTools.insert("compaction");
        const auto present = [&] {
            CodexTurnView view;
            int budget = 4096;
            populateTurnPresentation(turn, view, budget, true);
            return view;
        };
        auto view = present();
        QCOMPARE(view.progress.first().text,
                 QString("Compacting context \u00b7 This can take a few minutes"));
        QVERIFY(!view.thinking);
        // Compaction can complete while the same model turn continues thinking.
        turn.activeTools.clear();
        view = present();
        QVERIFY(view.thinking);
        QCOMPARE(view.progress.first().text, QString("Context automatically compacted"));
        auto row = codexWorkLayout(view, {});
        applyCodexViewIcons(row);
        QCOMPARE(workDetails(row).children.first().iconPath, QString("icons/list-collapse.svg"));
    }
    void liveUnphasedTextIsPromotedOnlyWhenItsTurnCompletes() {
        TurnState turn;
        turn.status = "inProgress";
        turn.items = {QJsonObject{{"type", "agentMessage"}, {"text", "Live response"}}};
        CodexTurnView live;
        int budget = 4096;
        populateTurnPresentation(turn, live, budget, true);
        QVERIFY(live.response.isEmpty());
        QVERIFY(live.thinking);
        QCOMPARE(live.progress.size(), 1);
        turn.status = "completed";
        CodexTurnView completed;
        budget = 4096;
        populateTurnPresentation(turn, completed, budget);
        QCOMPARE(completed.response, "Live response\n\n");
        QVERIFY(completed.progress.isEmpty());
    }
    void conversationTitleUsesRegularTextInsteadOfAHeading() {
        CodexViewState state;
        state.ready = true;
        state.historyLoaded = true;
        state.home = false;
        state.title = "Continue LiteCode terminal work";
        const auto layout = codexViewLayout(state);
        const auto find = [](auto&& self, const litecode::extensions::NativeControl& control)
            -> const litecode::extensions::NativeControl* {
            if (control.id == "codex.title")
                return &control;
            for (const auto& child : control.children)
                if (const auto* found = self(self, child))
                    return found;
            return nullptr;
        };
        const auto* title = find(find, layout);
        QVERIFY(title);
        QCOMPARE(title->appearance, litecode::extensions::NativeControlAppearance::Plain);
    }
    void imageHistoryAndComposerUseThumbnails() {
        TurnState turn;
        turn.items = {QJsonObject{
            {"type", "userMessage"},
            {"content",
             QJsonArray{QJsonObject{{"type", "text"}, {"text", "Look at these"}},
                        QJsonObject{{"type", "image"}, {"url", "data:image/png;base64,fixture"}},
                        QJsonObject{{"type", "localImage"}, {"path", "/image.png"}}}}}};
        CodexTurnView view;
        int budget = 4096;
        populateTurnPresentation(turn, view, budget);
        QCOMPARE(view.prompt, QString("Look at these"));
        QCOMPARE(view.images.size(), 2);
        QCOMPARE(view.images[1], QString("/image.png"));
        CodexViewState state;
        state.turns.append(view);
        auto layout = codexConversationLayout(state);
        const auto find = [](auto&& self, const litecode::extensions::NativeControl& node,
                             const QString& suffix) -> const litecode::extensions::NativeControl* {
            if (node.id.endsWith(suffix))
                return &node;
            for (const auto& child : node.children)
                if (const auto* match = self(self, child, suffix))
                    return match;
            return nullptr;
        };
        const auto* gallery = find(find, layout, ".images");
        QVERIFY(gallery && gallery->visible);
        QCOMPARE(gallery->children.size(), 3);
        QCOMPARE(gallery->children[1].kind, litecode::extensions::NativeControlKind::Image);
        state.turns[0].prompt = "  \r\n";
        layout = codexConversationLayout(state);
        QVERIFY(!find(find, layout, ".user")->visible);
        QVERIFY(!find(find, layout, ".userRow")->visible);
        QVERIFY(find(find, layout, ".images")->visible);
        QVERIFY(find(find, layout, ".userActions")->visible);
        state.attachments = {"/image.png", "/notes.txt"};
        layout = codexComposerLayout(state);
        QCOMPARE(find(find, layout, "attachment.0.name")->kind,
                 litecode::extensions::NativeControlKind::Image);
        const auto* preview = find(find, layout, "attachment.0.name");
        QCOMPARE(preview->value, 120);
        QCOMPARE(preview->children.size(), 1);
        QVERIFY(preview->children.first().toolTip.isEmpty());
        QCOMPARE(preview->children.first().accessibleName, QString("Remove attachment"));
        QVERIFY(find(find, layout, "codex.permissions")->toolTip.isEmpty());
        QCOMPARE(find(find, layout, "attachment.1.name")->kind,
                 litecode::extensions::NativeControlKind::Label);
        litecode::extensions::NativeControl copy;
        copy.id = "codex.turn.fixture.copyPrompt";
        copy.kind = litecode::extensions::NativeControlKind::Button;
        copy.text = "$(copy)";
        applyCodexViewIcons(copy);
        QCOMPARE(copy.iconSize, 12);
    }
    void imageCacheSurvivesTemporaryFilesAndRejectsInvalidData() {
        QTemporaryDir cache, temporary;
        CodexAttachmentStore store(cache.path());
        const QByteArray bytes =
            QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8D"
                                   "wHwAFgAI/ScLbtAAAAABJRU5ErkJggg==");
        const auto data = "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
        const auto saved = store.imagePath(data);
        QVERIFY(!saved.isEmpty());
        QFile original(temporary.filePath("clipboard.png"));
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write(bytes), bytes.size());
        original.close();
        QCOMPARE(store.imagePath(original.fileName()), saved);
        QVERIFY(original.remove());
        QVERIFY(QFileInfo::exists(saved));
        QCOMPARE(store.imagePath(data), saved);
        QVERIFY(store.imagePath("data:image/png;base64,invalid!data").isEmpty());
    }
    void nativeIconsAreOwnedByTheCodexExtension() {
        litecode::extensions::NativeControl button;
        button.id = "activity";
        button.kind = litecode::extensions::NativeControlKind::Button;
        button.appearance = litecode::extensions::NativeControlAppearance::Plain;
        button.text = "$(edit) Edited files, ran commands";
        applyCodexViewIcons(button);
        QCOMPARE(button.iconPath, QString("icons/pencil.svg"));
        QCOMPARE(button.iconSize, 12);
        QCOMPARE(button.text, QString("Edited files, ran commands"));
        QCOMPARE(codexViewIconText("$(terminal) Ran echo fixture"), QString("Ran echo fixture"));
        QCOMPARE(codexViewIconText("$(loading~spin)"), QString("$(loading~spin)"));
    }
    void userBubbleOmitsTrailingLineBreaksWithoutChangingTheOriginalPrompt() {
        CodexViewState state;
        CodexTurnView turn;
        for (const auto& ending : {QString("\n"), QString("\r\n"), QString("\n\n")}) {
            turn.prompt = "cont" + ending;
            QCOMPARE(codexPromptText(turn, state), QString("cont"));
            QCOMPARE(turn.prompt, "cont" + ending);
        }
        turn.prompt = "    indented first line\n\n    indented last line  \r\n";
        QCOMPARE(codexPromptText(turn, state),
                 QString("    indented first line\n\n    indented last line  "));
    }
    void historyProjectionKeepsEarlierMessagesWhenWorkIsLarge() {
        QVector<TurnState> turns;
        for (int i = 0; i < 12; ++i) {
            TurnState turn;
            turn.id = QString::number(i);
            turn.status = "completed";
            turn.diff = QString(70000, '+');
            turn.items.append(QJsonObject{
                {"type", "userMessage"},
                {"id", "user"},
                {"content",
                 QJsonArray{QJsonObject{{"type", "text"}, {"text", QString(60000, 'p')}}}}});
            for (int j = 0; j < 50; ++j)
                turn.items.append(QJsonObject{{"type", "commandExecution"},
                                              {"command", "build"},
                                              {"aggregatedOutput", QString(16000, 'x')}});
            turn.items.append(QJsonObject{{"type", "agentMessage"},
                                          {"phase", "final_answer"},
                                          {"text", "Answer " + turn.id}});
            turns.append(turn);
        }
        CodexViewState state;
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.size(), 8);
        QCOMPARE(state.turns.first().turnId, QString("4"));
        QCOMPARE(state.turns.last().response.trimmed(), QString("Answer 11"));
        QCOMPARE(state.turns.last().prompt.size(), 601);
        QVERIFY(state.hasOlder);
        state.expandedPrompts.insert(state.turns.last().key);
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.last().prompt.size(), 60000);
        prependCodexHistoryWindow(state, turns, state.turns.first().turnId);
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.first().turnId, QString("0"));
        QCOMPARE(state.turns.last().turnId, QString("11"));
        QVERIFY(!state.hasOlder);
        // Optional local diagnostic data never becomes a checked-in chat fixture.
        const auto capture = qEnvironmentVariable("LITECODE_HISTORY_CAPTURE");
        if (!capture.isEmpty()) {
            QFile file(capture);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto data = QJsonDocument::fromJson(file.readAll())
                                  .array()
                                  .first()
                                  .toObject()
                                  .value("page")
                                  .toObject()
                                  .value("data")
                                  .toArray();
            turns.clear();
            for (auto it = data.end(); it != data.begin();) {
                TurnState turn;
                readTurnHistory((*--it).toObject(), turn);
                turns.append(turn);
            }
            state = {};
            projectCodexHistoryWindow(state, turns, true);
            QCOMPARE(state.turns.size(), qMin(8, int(turns.size())));
        }
    }
    void loadingOlderHistoryRetainsTheMessagesAlreadyOnScreen() {
        QVector<TurnState> turns;
        for (int i = 0; i < 40; ++i) {
            TurnState turn;
            turn.id = QString::number(i);
            turn.status = "completed";
            turn.items = {QJsonObject{{"type", "agentMessage"},
                                      {"phase", "final_answer"},
                                      {"text", "Answer " + turn.id}}};
            turns.append(turn);
        }
        CodexViewState state;
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.first().turnId, QString("32"));
        for (int expectedFirst : {24, 16, 8}) {
            prependCodexHistoryWindow(state, turns, state.turns.first().turnId);
            projectCodexHistoryWindow(state, turns, false);
            QCOMPARE(state.turns.first().turnId, QString::number(expectedFirst));
            QCOMPARE(state.turns.last().turnId, QString("39"));
        }
        prependCodexHistoryWindow(state, turns, state.turns.first().turnId);
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.first().turnId, QString("0"));
        QVERIFY(state.turns.size() <= 32);
        QVERIFY(std::any_of(state.turns.begin(), state.turns.end(),
                            [](const auto& turn) { return turn.turnId == "8"; }));
        appendCodexHistoryWindow(state, turns);
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.last().turnId, QString("39"));
    }
    void oversizedHistoryAnchorStillAdvancesToEarlierTurns() {
        QVector<TurnState> turns;
        for (int i = 0; i < 3; ++i) {
            TurnState turn;
            turn.id = QString::number(i);
            turn.status = "completed";
            turn.items = {
                QJsonObject{{"type", "agentMessage"},
                            {"phase", "final_answer"},
                            {"text", i == 2 ? QString(2000000, 'x') : QString("Earlier answer")}}};
            turns.append(turn);
        }
        CodexViewState state;
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.size(), 1);
        state.turnWindowEndId = codexOlderWindowEnd(turns, state.turns.first().turnId, false);
        QCOMPARE(state.turnWindowEndId, QString("1"));
        projectCodexHistoryWindow(state, turns, false);
        QCOMPARE(state.turns.size(), 2);
        QCOMPARE(state.turns.first().turnId, QString("0"));
        QVERIFY(!state.hasOlder);
        QVERIFY(codexOlderWindowEnd(turns, "0", false).isEmpty());
        TurnState older;
        older.id = "older";
        turns.prepend(older); // A server page has just supplied the missing predecessor.
        QCOMPARE(codexOlderWindowEnd(turns, "0", false), QString("older"));
    }
    void modelsWithoutEffortRemainSelectableAndGoalsRequireAnObjective() {
        const auto find = [](const auto& self, const litecode::extensions::NativeControl& root,
                             const QString& id) -> const litecode::extensions::NativeControl* {
            if (root.id == id)
                return &root;
            for (const auto& child : root.children)
                if (const auto* result = self(self, child, id))
                    return result;
            return nullptr;
        };
        CodexViewState state;
        state.ready = true;
        state.model = "without-effort";
        state.models = {
            QJsonObject{{"id", "without-effort"}, {"displayName", "No reasoning controls"}}};
        auto layout = codexComposerLayout(state);
        const auto* picker = find(find, layout, "codex.modelPicker");
        QVERIFY(picker);
        QCOMPARE(picker->children.size(), 1);
        QCOMPARE(picker->children.first().id, "codex.model");
        QVERIFY(picker->children.first().visible);
        QVERIFY(picker->children.first().enabled);
        state.goalMode = true;
        state.attachments = {"image.png"};
        QVERIFY(!codexCanSend(state));
        state.draft = "Implement the requested change";
        QVERIFY(codexCanSend(state));
        state.readOnly = true;
        state.home = false;
        QVERIFY(!codexCanSend(state));
    }
    void conversationOptionsContainOnlyConversationActions() {
        const auto optionsFor = [](const CodexViewState& state) {
            const auto find = [](auto&& self, const litecode::extensions::NativeControl& node)
                -> litecode::extensions::NativeControl {
                if (node.id == "codex.options")
                    return node;
                for (const auto& child : node.children) {
                    auto found = self(self, child);
                    if (!found.id.isEmpty())
                        return found;
                }
                return {};
            };
            return find(find, codexViewLayout(state));
        };
        CodexViewState state;
        state.historyLoaded = true;
        state.home = false;
        auto options = optionsFor(state);
        QCOMPARE(options.id, "codex.options");
        QVERIFY(options.items.isEmpty());
        QCOMPARE(options.children.size(), 5);
        QCOMPARE(options.children[0].text, "Rename");
        QCOMPARE(options.children[2].text, "Copy");
        QCOMPARE(options.children[2].children.size(), 3);
        QCOMPARE(options.children[2].children[0].text, QString("Copy deeplink"));
        QCOMPARE(options.children[2].children[1].text, QString("Copy as Markdown"));
        QCOMPARE(options.children[2].children[2].text, QString("Copy working directory"));
        QCOMPARE(options.children[4].text, "Archive");
        QVERIFY(options.visible);
        state.writable = false;
        options = optionsFor(state);
        QVERIFY(!options.children[0].enabled);
        QVERIFY(!options.children[4].enabled);
        QVERIFY(options.children[2].enabled);
        state.home = true;
        QVERIFY(!optionsFor(state).visible);
        state.home = false;
        state.opening = true;
        QVERIFY(!optionsFor(state).visible);
    }
    void accountMenuUsesLiveIdentityAndOnlyImplementedActions() {
        CodexViewState state;
        state.signedIn = true;
        state.accountLabel = "fixture@example.test";
        state.accountKind = "Personal account";
        auto menu = codexAccountMenu(state);
        QCOMPARE(menu[0].kind, litecode::extensions::NativeControlKind::Label);
        QCOMPARE(menu[0].text, state.accountLabel);
        QCOMPARE(menu[1].text, state.accountKind);
        QCOMPARE(menu[3].text, QString("Codex settings"));
        QCOMPARE(menu[4].text, QString("Keyboard shortcuts"));
        QCOMPARE(menu.last().text, QString("Log out"));
        state.signedIn = false;
        QCOMPARE(codexAccountMenu(state).last().text, QString("Sign in"));
        state.signingIn = true;
        QCOMPARE(codexAccountMenu(state).last().text, QString("Cancel sign-in"));
        QCOMPARE(codexAccountMenu(state, "codex.localMenu.").first().id,
                 QString("codex.localMenu.identity"));
    }
    void startupUsesLogoAndLaterLoadsUseSpinners() {
        const auto find = [](const auto& self, const litecode::extensions::NativeControl& root,
                             const QString& id) -> const litecode::extensions::NativeControl* {
            if (root.id == id)
                return &root;
            for (const auto& child : root.children)
                if (const auto* result = self(self, child, id))
                    return result;
            return nullptr;
        };
        CodexViewState state;
        auto layout = codexViewLayout(state);
        const auto* logo = find(find, layout, "codex.startupLogo");
        QVERIFY(logo);
        QCOMPARE(logo->iconPath, "icons/codex-home.svg");
        QCOMPARE(logo->value, 56);
        QVERIFY(!logo->enabled);
        QVERIFY(!find(find, layout, "codex.composer"));
        state.ready = true;
        state.historyLoaded = true;
        state.home = false;
        state.opening = true;
        layout = codexViewLayout(state);
        QVERIFY(!find(find, layout, "codex.startupLogo"));
        QVERIFY(find(find, layout, "codex.openLoading")->visible);
        QCOMPARE(find(find, layout, "codex.openSpinner")->text, "$(loading~spin)");
        QVERIFY(!find(find, layout, "codex.openMessage")->visible);
        QVERIFY(!find(find, layout, "codex.historyPanel")->visible);
        QVERIFY(!find(find, layout, "codex.conversation")->visible);
        QVERIFY(!find(find, layout, "codex.composer")->visible);
        state.home = true; // Back cancels navigation, even if the read is pending.
        layout = codexViewLayout(state);
        QVERIFY(!find(find, layout, "codex.openLoading")->visible);
        QVERIFY(find(find, layout, "codex.historyPanel")->visible);
        state.home = false;
        state.opening = false;
        state.openError = "Couldn't open fixture chat";
        layout = codexViewLayout(state);
        QVERIFY(find(find, layout, "codex.openMessage")->visible);
        QVERIFY(find(find, layout, "codex.retryOpen")->visible);
        state.historyLoading = true;
        state.home = true;
        layout = codexHistoryLayout(state);
        QVERIFY(find(find, layout, "codex.historySpinner")->visible);
        QVERIFY(!find(find, layout, "codex.emptyHistory")->visible);
    }
    void missingTimingDoesNotInventZeroSecondsOrHideUnfinishedWork() {
        TurnState turn;
        turn.status = "interrupted";
        turn.items = {QJsonObject{{"type", "agentMessage"},
                                  {"phase", "commentary"},
                                  {"text", "Work already performed."}}};
        CodexTurnView view;
        int budget = 4096;
        populateTurnPresentation(turn, view, budget);
        QCOMPARE(view.workLabel, QString("Work details"));
        QVERIFY(!view.hasWorkTiming);
        QVERIFY(view.responseTime.isEmpty());
        QVERIFY(codexWorkExpanded(view, CodexViewState{}));
        CodexTurnView running;
        budget = 4096;
        populateTurnPresentation(turn, running, budget, true);
        QCOMPARE(running.workLabel, QString("Working"));
    }
    void missingTimingAndActivityDoNotLeaveAnEmptyDisclosure() {
        TurnState turn;
        turn.status = "completed";
        turn.items = {
            QJsonObject{{"type", "agentMessage"}, {"phase", "final_answer"}, {"text", "Done."}}};
        CodexTurnView view;
        int budget = 4096;
        populateTurnPresentation(turn, view, budget);
        QVERIFY(!codexWorkLayout(view, {}).visible);
        turn.durationMs = 14000;
        populateTurnPresentation(turn, view, budget);
        const auto layout = codexWorkLayout(view, {});
        QVERIFY(layout.visible);
        QCOMPARE(layout.children.first().text, QString("Worked for 14s"));
        QCOMPARE(layout.children.first().kind, litecode::extensions::NativeControlKind::Label);
    }
    void completedWorkIsSeparateFromTheFinalAnswer() {
        TurnState turn;
        turn.status = "completed";
        turn.durationMs = 476000;
        turn.items = {
            {{"type", "agentMessage"}, {"phase", "commentary"}, {"text", "First progress update."}},
            {{"type", "commandExecution"},
             {"command", "echo fixture"},
             {"aggregatedOutput", "fixture"}},
            {{"type", "agentMessage"},
             {"phase", "commentary"},
             {"text", "Second progress update."}},
            {{"type", "agentMessage"},
             {"phase", "final_answer"},
             {"text", "Final answer.\n\nSecond paragraph."}}};
        turn.diff = "diff --git a/a.cpp b/a.cpp\n--- a/a.cpp\n+++ b/a.cpp\n@@ -1 +1,2 "
                    "@@\n-old\n+new\n+more\n";
        int budget = 128 * 1024;
        CodexTurnView view;
        populateTurnPresentation(turn, view, budget);
        QCOMPARE(view.workLabel, "Worked for 7m 56s");
        QCOMPARE(view.progress.size(), 3);
        QCOMPARE(view.progress[1].text, "Ran commands");
        QVERIFY(view.progress[1].details.contains("echo fixture"));
        QVERIFY(!view.response.contains("progress update"));
        QCOMPARE(view.response, codexFinalResponse(turn));
        QCOMPARE(view.files.size(), 1);
        QCOMPARE(view.files[0].path, "a.cpp");
        QCOMPARE(view.files[0].additions, 2);
        QCOMPARE(view.files[0].deletions, 1);
        QVERIFY(codexFileDiff(turn.diff, "a.cpp").contains("+more"));
        QVERIFY(codexFileDiff(turn.diff, "missing.cpp").isEmpty());
    }
    void runningWorkHasNoCompletedDisclosure() {
        TurnState turn;
        turn.id = "running";
        turn.status = "inProgress";
        turn.startedMs = QDateTime::currentMSecsSinceEpoch() - 14000;
        turn.items = {QJsonObject{
            {"type", "agentMessage"}, {"phase", "commentary"}, {"text", "Checking the code."}}};
        CodexTurnView view;
        view.key = turn.id;
        int budget = 4096;
        populateTurnPresentation(turn, view, budget, true);
        CodexViewState state;
        state.expandedActivity.insert(view.key + "/collapsed");
        auto layout = codexWorkLayout(view, state);
        QVERIFY(layout.children[0].visible);
        QVERIFY(layout.children[1].visible);
        QCOMPARE(layout.children[0].kind, litecode::extensions::NativeControlKind::Label);
        QVERIFY(layout.children[0].text.startsWith("Working for "));
        QVERIFY(!layout.children[0].text.contains("$(chevron"));
        QVERIFY(workDetails(layout).visible);
        QCOMPARE(workDetails(layout).children.first().text, QString("Checking the code."));
        turn.status = "completed";
        turn.durationMs = 14000;
        view = {};
        view.key = turn.id;
        populateTurnPresentation(turn, view, budget);
        layout = codexWorkLayout(view, state);
        QVERIFY(layout.children[0].visible);
        QVERIFY(layout.children[1].visible);
        QVERIFY(layout.children[0].text.startsWith("Worked for 14s"));
        QVERIFY(!workDetails(layout).visible);
    }
    void commandGroupsPreserveCommentaryAndExpansionState() {
        CodexTurnView turn;
        turn.key = "fixture";
        turn.progress = {{"Before", {}, {}},
                         {"Ran commands", "echo first\noutput", "terminal"},
                         {"Ran commands", "echo second\noutput", "terminal"},
                         {"After", {}, {}},
                         {"Ran commands", "echo third", "terminal"}};
        CodexViewState state;
        state.expandedActivity.insert(turn.key);
        auto layout = codexWorkLayout(turn, state);
        auto details = workDetails(layout);
        QCOMPARE(details.children.size(), 4);
        QVERIFY(details.children[1].children.first().id.endsWith(".activityGroup.1"));
        QCOMPARE(details.children[1].children.first().text, QString("$(terminal) Ran commands"));
        QCOMPARE(details.children[1].children.first().appearance,
                 litecode::extensions::NativeControlAppearance::Muted);
        QVERIFY(details.children[3].children.first().id.endsWith(".activityGroup.4"));
        QVERIFY(!codexProgressVisible(turn, state, 2));
        state.expandedActivity.insert("fixture/activity.1");
        layout = codexWorkLayout(turn, state);
        details = workDetails(layout);
        QCOMPARE(details.children.size(), 4);
        QCOMPARE(details.children[1].children.size(), 3);
        QCOMPARE(details.children[1].children[1].text, "$(terminal) Ran echo first");
        QCOMPARE(details.children[1].children[2].text, "$(terminal) Ran echo second");
        QVERIFY(codexProgressVisible(turn, state, 2));
        QVERIFY(!codexProgressVisible(turn, state, 4));
        state.expandedActivity.remove(turn.key);
        QVERIFY(!codexProgressVisible(turn, state, 2));
    }
    void consecutiveCommandsAndEditsShareOneActivitySummary() {
        CodexTurnView turn;
        turn.key = "mixed";
        turn.progress = {{"Commentary", {}, {}},
                         {"Ran commands", "echo check", "terminal"},
                         {"Edited files", "one.cpp", "edit"},
                         {"Edited files", "two.cpp", "edit"},
                         {"Next commentary", {}, {}}};
        CodexViewState state;
        state.expandedActivity.insert(turn.key);
        auto layout = codexWorkLayout(turn, state);
        auto details = workDetails(layout);
        QCOMPARE(details.children.size(), 3);
        QCOMPARE(details.children[1].children.size(), 1);
        QCOMPARE(details.children[1].children.first().text,
                 QString("$(edit) Edited files, ran commands"));
        QVERIFY(!codexProgressVisible(turn, state, 2));
        state.expandedActivity.insert("mixed/activity.1");
        state.expandedActivity.insert("mixed/2");
        layout = codexWorkLayout(turn, state);
        details = workDetails(layout);
        QCOMPARE(details.children[1].children.size(), 5);
        QCOMPARE(details.children[1].children[3].text, QString("one.cpp"));
        QVERIFY(codexProgressVisible(turn, state, 3));
        QCOMPARE(codexActivityGroupStart(turn, 3), 1);
    }
    void timestampsUseTurnTimesAndMissingTimesStayEmpty() {
        TurnState turn;
        turn.startedMs = 1700000000000;
        turn.durationMs = 90000;
        CodexTurnView view;
        int budget = 1024;
        populateTurnPresentation(turn, view, budget, false);
        const auto started = QDateTime::fromMSecsSinceEpoch(turn.startedMs).toLocalTime();
        QCOMPARE(view.userTime, QLocale().toString(started.time(), QLocale::ShortFormat));
        QCOMPARE(view.responseTime,
                 QLocale().toString(started.addMSecs(90000).time(), QLocale::ShortFormat));
        CodexTurnView unknown;
        turn.startedMs = 0;
        populateTurnPresentation(turn, unknown, budget, false);
        QVERIFY(unknown.userTime.isEmpty());
        QVERIFY(unknown.responseTime.isEmpty());
    }
    void savedFileChangesStillHaveAReviewDiff() {
        TurnState turn;
        turn.items = {
            {{"type", "fileChange"},
             {"changes", QJsonArray{QJsonObject{{"path", "saved.py"},
                                                {"diff", "@@ -1 +1 @@\n-before\n+after\n"}}}}}};
        int budget = 128 * 1024;
        CodexTurnView view;
        populateTurnPresentation(turn, view, budget);
        QVERIFY(codexTurnDiff(turn).contains("+++ b/saved.py"));
        QCOMPARE(view.files.size(), 1);
        QCOMPARE(view.files[0].additions, 1);
        QCOMPARE(view.files[0].deletions, 1);
    }
    void olderUnphasedResponsesUseTheLastAnswer() {
        TurnState turn;
        turn.items = {{{"type", "agentMessage"}, {"text", "Progress."}},
                      {{"type", "agentMessage"}, {"text", "Finished."}}};
        int budget = 128 * 1024;
        CodexTurnView view;
        populateTurnPresentation(turn, view, budget);
        QCOMPARE(view.progress.size(), 1);
        QCOMPARE(view.response.trimmed(), "Finished.");
    }
    void finalAnswerSurvivesAnOversizedProgressHistory() {
        TurnState turn;
        turn.items = {
            {{"type", "userMessage"},
             {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QString(10000, 'p')}}}}},
            {{"type", "agentMessage"}, {"phase", "commentary"}, {"text", QString(10000, 'x')}},
            {{"type", "agentMessage"},
             {"phase", "final_answer"},
             {"text", "Important final answer."}}};
        int budget = 100;
        CodexTurnView view;
        populateTurnPresentation(turn, view, budget);
        QVERIFY(view.response.contains("Important final answer."));
        QVERIFY(budget >= 0);
    }
    void searchAndImageActivityRemainInTheWorkSection() {
        TurnState turn;
        turn.items = {{{"type", "webSearch"}, {"query", "fixture search"}},
                      {{"type", "imageView"}, {"path", "fixture.png"}}};
        int budget = 1024;
        CodexTurnView view;
        populateTurnPresentation(turn, view, budget);
        QCOMPARE(view.progress.size(), 2);
        QCOMPARE(view.progress[0].text, "Searched the web");
        QCOMPARE(view.progress[1].text, "Viewed an image");
        QVERIFY(view.response.isEmpty());
    }
};
QTEST_GUILESS_MAIN(CodexPresentationTests)
#include "tst_codex_presentation.moc"
