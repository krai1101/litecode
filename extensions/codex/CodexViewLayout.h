#pragma once
#include "CodexApproval.h"
#include "CodexExecutionPolicy.h"
#include <QJsonArray>
#include <QSet>
#include <extensions/NativeViewApi.h>
namespace litecode::codex {
inline constexpr int CodexContentInset = 8;
struct CodexProgressView {
    QString text, details, icon;
    bool running{};
    QString command;
};
struct CodexFileChangeView {
    QString path;
    int additions{}, deletions{};
};
struct CodexTurnView {
    QString key, turnId, userItemId, prompt, response, activity, diff, error, status;
    QString workLabel, userTime, responseTime;
    QStringList images;
    QVector<CodexProgressView> progress;
    QVector<CodexFileChangeView> files;
    bool undoAvailable{};
    bool hasWorkTiming{};
    bool thinking{}, responseStreaming{};
};
struct CodexViewState {
    std::optional<CodexApprovalPrompt> approval;
    QString approvalKey;
    bool approvalExpanded{};
    QString draft, model, effort{"medium"}, notice;
    QString copiedControlId;
    QJsonArray models, threads;
    QStringList attachments;
    bool busy{}, history{}, includeEditor{}, editing{}, fastMode{};
    bool home{true};
    bool planMode{}, goalMode{};
    ApprovalMode approvalMode{ApprovalMode::Ask};
    bool opening{}, readOnly{}, writable{true};
    QString openError;
    bool ready{}, hasOlder{};
    QString title, accountLabel, accountKind, goalLabel, goalObjective;
    bool signedIn{}, signingIn{};
    bool accountKnown{}, accountChecking{}, requiresAuth{true}, authBusy{}, apiKeyEntry{}, confirmingLogout{};
    QString deviceCode;
    QString workspace;
    qint64 contextUsed{}, contextLimit{};
    bool goalExpanded{};
    bool renaming{};
    QString renameDraft;
    QString historyFilter, historyCursor, historyError;
    QString turnWindowEndId;
    int historyWindowSize{8};
    bool historyLoading{}, historyMore{}, historyLoaded{};
    QVector<CodexTurnView> turns;
    QSet<QString> expandedActivity;
    QSet<QString> expandedPrompts;
    QSet<QString> expandedChanges;
};
QString codexPromptText(const CodexTurnView& turn, const CodexViewState& state);
QVector<extensions::NativeControl> codexConversationMenu(const CodexViewState& state);
QVector<extensions::NativeControl> codexAccountMenu(const CodexViewState& state,
                                                       const QString& prefix = "codex.menu.");
extensions::NativeControl codexConversationLayout(const CodexViewState& state);
int codexActivityGroupStart(const CodexTurnView& turn, int index);
bool codexProgressVisible(const CodexTurnView& turn, const CodexViewState& state, int index);
QString codexToolLabel(const CodexProgressView& item);
bool codexWorkExpanded(const CodexTurnView& turn, const CodexViewState& state);
extensions::NativeControl codexWorkLayout(const CodexTurnView& turn, const CodexViewState& state);
extensions::NativeControl codexChangesLayout(const CodexTurnView& turn, const CodexViewState& state,
                                             bool latest);
extensions::NativeControl codexHistoryLayout(const CodexViewState& state);
extensions::NativeControl codexComposerLayout(const CodexViewState& state);
extensions::NativeControl codexApprovalLayout(const CodexViewState& state);
bool codexStarting(const CodexViewState& state);
bool codexCanSend(const CodexViewState& state);
QJsonObject codexFastTier(const CodexViewState& state);
QVector<QJsonObject> codexFilteredThreads(const CodexViewState& state);
extensions::NativeControl codexViewLayout(const CodexViewState& state);
bool codexSignedOut(const CodexViewState& state);
extensions::NativeControl codexAuthLayout(const CodexViewState& state);
extensions::NativeControl codexLogoutDialog(const CodexViewState& state);
} // namespace litecode::codex
