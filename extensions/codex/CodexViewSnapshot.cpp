#include "CodexHistoryWindow.h"
#include "CodexTurnPresentation.h"
#include "CodexViewIcons.h"
#include "CodexViewProvider.h"
namespace litecode::codex {
void CodexViewProvider::updatePresentation() {
    state_.busy = session_.busy();
    state_.ready = session_.server().ready();
    state_.opening = session_.opening();
    state_.readOnly = session_.readOnly();
    state_.writable = session_.canWrite();
    state_.title = session_.title();
    if ((state_.opening || !state_.openError.isEmpty()) && followOpenedThread_) {
        state_.title = session_.threadId() == lastOpenThread_ ? session_.title() : QString("Chat");
        for (const auto& value : state_.threads) {
            const auto thread = value.toObject();
            if (thread.value("id").toString() != lastOpenThread_)
                continue;
            state_.title = thread.value("name").toString().trimmed();
            if (state_.title.isEmpty())
                state_.title = thread.value("preview").toString().simplified().left(100);
            if (state_.title.isEmpty())
                state_.title = "Untitled chat";
            break;
        }
    }
    state_.hasOlder = session_.hasOlderTurns();
    const auto account = session_.account();
    state_.accountLabel =
        account.value("email").toString(account.isEmpty() ? "Sign in to Codex" : "Signed in");
    state_.signedIn = !account.isEmpty();
    state_.signingIn = session_.signingIn();
    state_.accountKnown = session_.accountKnown();
    state_.accountChecking = !state_.accountKnown && state_.notice.isEmpty();
    state_.requiresAuth = session_.requiresAuth();
    state_.authBusy = session_.authBusy();
    state_.deviceCode = session_.deviceCode();
    const auto plan = account.value("planType").toString().toLower();
    state_.accountKind = account.value("type") == "apiKey" ? "API key account"
        : plan == "free" || plan == "plus" || plan == "pro" ? "Personal account"
        : plan == "team" || plan == "business" || plan == "enterprise" ? "Business account"
        : "ChatGPT account";
    state_.workspace = session_.threadWorkspace();
    const auto usage = session_.usage();
    state_.contextUsed =
        qMax<qint64>(0, usage.value("last").toObject().value("totalTokens").toInteger());
    state_.contextLimit = qMax<qint64>(0, usage.value("modelContextWindow").toInteger());
    const auto goal = session_.goal();
    state_.goalLabel = goal.isEmpty() ? QString{} : "Goal " + goal.value("status").toString();
    state_.goalObjective = goal.value("objective").toString().left(16 * 1024);
    projectCodexHistoryWindow(state_, session_.turns(), session_.hasOlderTurns());
    for (auto& turn : state_.turns)
        for (auto& source : turn.images)
            source = attachmentStore_.imagePath(source);
}
QString CodexViewProvider::presentationSignature() const {
    QString signature = state_.approvalKey + QString::number(state_.approvalExpanded) +
                        QString::number(state_.busy) + QString::number(state_.ready) +
                        QString::number(state_.fastMode) + QString::number(state_.opening) +
                        QString::number(state_.readOnly) + QString::number(state_.writable) +
                        QString::number(state_.hasOlder) + state_.title + '\n' +
                        state_.accountLabel + state_.accountKind + QString::number(state_.signedIn) +
                        QString::number(state_.signingIn) + QString::number(state_.accountKnown) +
                        QString::number(state_.requiresAuth) + QString::number(state_.accountChecking) +
                        QString::number(state_.authBusy) +
                        QString::number(state_.apiKeyEntry) + QString::number(state_.confirmingLogout) +
                        state_.deviceCode + '\n' + QString::number(state_.contextUsed) + ':' +
                        QString::number(state_.contextLimit) + '\n' + state_.goalLabel + '\n' +
                        state_.goalObjective;
    for (const auto& turn : state_.turns) {
        signature +=
            turn.key + turn.status + QString::number(turn.thinking) +
            QString::number(turn.responseStreaming) +
            QString::number(codexWorkExpanded(turn, state_)) + turn.userTime + turn.responseTime +
            QString::number(turn.prompt.isEmpty()) + QString::number(turn.response.isEmpty()) +
            QString::number(turn.hasWorkTiming) + QString::number(turn.prompt.size() > 600) +
            QString::number(turn.progress.size()) + turn.diff +
            QString::number(turn.error.isEmpty());
        for (const auto& file : turn.files)
            signature += file.path + ':' + QString::number(file.additions) + ':' +
                         QString::number(file.deletions) + '\n';
        for (const auto& activity : turn.progress)
            signature += '\n' + activity.icon;
        for (const auto& image : turn.images)
            signature += '\n' + image;
    }
    return signature;
}
void CodexViewProvider::refresh() {
    if (!view_)
        return;
    const auto previous = state_.turns;
    updatePresentation();
    if (presentationSignature() != layoutSignature_) {
        render();
        return;
    }
    // Startup deliberately contains only its logo, so it has no incremental
    // conversation controls to update until the first real view is rendered.
    if (codexStarting(state_))
        return;
    for (int i = 0; i < state_.turns.size(); ++i) {
        const auto& turn = state_.turns[i];
        const auto id = "codex.turn." + turn.key;
        const auto before = i < previous.size() ? previous[i] : CodexTurnView{};
        if (turn.prompt != before.prompt)
            view_->setText(id + ".prompt", codexPromptText(turn, state_));
        if (turn.response != before.response)
            view_->setText(id + ".response", turn.response);
        if (turn.workLabel != before.workLabel)
            view_->setText(id + ".tools",
                           turn.workLabel +
                               (turn.progress.isEmpty() || turn.status == "inProgress" ||
                                        turn.status == "in_progress"
                                    ? QString{}
                                    : (codexWorkExpanded(turn, state_) ? " $(chevron-down)"
                                                                       : " $(chevron-right)")));
        if (codexWorkExpanded(turn, state_))
            for (int j = 0; j < turn.progress.size(); ++j) {
                const auto prefix = id + ".progress." + QString::number(j);
                if (turn.progress[j].icon.isEmpty())
                    view_->setText(prefix, turn.progress[j].text);
                else if (codexProgressVisible(turn, state_, j)) {
                    view_->setText(id + ".tool." + QString::number(j),
                                   codexViewIconText(codexToolLabel(turn.progress[j])));
                    if (state_.expandedActivity.contains(turn.key + "/" + QString::number(j)))
                        view_->setText(prefix + ".details", turn.progress[j].details);
                }
            }
        if (turn.error != before.error)
            view_->setText(id + ".error", turn.error);
    }
}
} // namespace litecode::codex
