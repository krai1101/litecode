#include "CodexViewControls.h"
#include <QJsonObject>
namespace litecode::codex {
using namespace extensions;
using namespace view;
QVector<QJsonObject> codexFilteredThreads(const CodexViewState& s) {
    QVector<QJsonObject> result;
    for (const auto& value : s.threads) {
        const auto thread = value.toObject();
        if (s.historyFilter.isEmpty() ||
            thread.value("name").toString().contains(s.historyFilter, Qt::CaseInsensitive) ||
            thread.value("preview").toString().contains(s.historyFilter, Qt::CaseInsensitive))
            result.append(thread);
    }
    return result;
}
bool codexStarting(const CodexViewState& s) {
    return s.accountChecking || (s.models.isEmpty() && !s.historyLoaded && s.turns.isEmpty() &&
           !s.opening && s.notice.isEmpty() && s.openError.isEmpty() && s.historyError.isEmpty());
}
NativeControl codexViewLayout(const CodexViewState& s) {
    auto root = control("codex.root", NativeControlKind::Column);
    root.appearance = NativeControlAppearance::Transcript;
    root.margin = CodexContentInset;
    root.spacing = 6;
    if (codexSignedOut(s))
        return codexAuthLayout(s);
    if (codexStarting(s)) {
        auto row = control("codex.startupRow", NativeControlKind::Row);
        auto logo = action("codex.startupLogo", {}, "Starting Codex", false);
        logo.iconPath = "icons/codex-home.svg";
        logo.value = 56;
        row.children = {spacer("codex.startupLeft"), logo, spacer("codex.startupRight")};
        root.children = {spacer("codex.startupAbove"), row, spacer("codex.startupBelow")};
        return root;
    }
    const bool loadingConversation = s.opening && !s.home && !s.history;
    const bool conversationPending =
        loadingConversation || (!s.home && !s.history && !s.openError.isEmpty());
    if (!conversationPending)
        root.contentsMargins = QMargins(CodexContentInset, CodexContentInset, CodexContentInset, 20);
    auto content = control("codex.content", NativeControlKind::Column);
    content.margin = 0;
    content.contentsMargins = QMargins(CodexContentInset, 0, 0, 0);
    content.spacing = 6;
    content.stretch = 1;
    auto header = control("codex.toolbar", NativeControlKind::Row);
    header.spacing = 0;
    header.contentsMargins = QMargins(0, 0, CodexContentInset, 0);
    auto title = control("codex.title", NativeControlKind::Label,
                         s.history || s.home ? "Chats"
                         : s.title.isEmpty() ? "New chat"
                                             : s.title);
    title.appearance = NativeControlAppearance::Plain;
    title.stretch = 1;
    auto back = action("codex.back", "$(arrow-left)", s.history ? "Back" : "Back to chats");
    back.toolTip = back.accessibleName;
    back.appearance = NativeControlAppearance::Plain;
    back.value = 12;
    back.visible = s.history || !s.home;
    auto options = action("codex.options", "$(ellipsis)", "Conversation options", s.ready);
    options.toolTip = options.accessibleName;
    options.visible = !s.home && !s.history && !conversationPending;
    options.children = codexConversationMenu(s);
    auto newChat = action("codex.new", "$(new-file)", "New chat", !s.busy);
    newChat.toolTip = newChat.accessibleName;
    auto history = action("codex.history", "$(history)", "Chat history", !s.busy);
    history.toolTip = history.accessibleName;
    auto settings = action("codex.settings", "$(settings)", "Settings", s.ready);
    settings.toolTip = settings.accessibleName;
    settings.children = codexAccountMenu(s);
    header.children = {back, title, options, history, settings, newChat};
    content.children.append(header);
    auto notice = control("codex.notice", NativeControlKind::Label, s.notice);
    notice.appearance = NativeControlAppearance::Muted;
    notice.visible = !s.notice.isEmpty();
    content.children.append(notice);
    auto openStatus = control("codex.openStatus", NativeControlKind::Column);
    openStatus.appearance = NativeControlAppearance::Default;
    openStatus.margin = 0;
    openStatus.visible = loadingConversation || !s.openError.isEmpty();
    openStatus.stretch = loadingConversation ? 1 : 0;
    auto loading = control("codex.openLoading", NativeControlKind::Column);
    loading.visible = loadingConversation;
    loading.stretch = 1;
    auto spinnerRow = control("codex.openSpinnerRow", NativeControlKind::Row);
    spinnerRow.children = {
        spacer("codex.openLoadingLeft"),
        action("codex.openSpinner", "$(loading~spin)", "Loading conversation", false),
        spacer("codex.openLoadingRight")};
    loading.children = {spacer("codex.openLoadingAbove"), spinnerRow,
                        spacer("codex.openLoadingBelow")};
    auto openMessage =
        control("codex.openMessage", NativeControlKind::Label,
                !s.openError.isEmpty()
                    ? s.openError
                    : "This chat is open in another Codex session. You can read it here.");
    openMessage.visible = !s.opening && !s.openError.isEmpty();
    auto openActions = control("codex.openActions", NativeControlKind::Row);
    auto retryOpen = action("codex.retryOpen", "$(refresh) Retry", "Retry opening this chat",
                            s.ready && !s.busy);
    retryOpen.visible = !s.opening;
    auto dismissOpen = action("codex.dismissOpen", "Dismiss", "Dismiss this error");
    dismissOpen.visible = !s.openError.isEmpty() && !s.opening;
    openActions.visible = !s.opening && !s.openError.isEmpty();
    openActions.children = {retryOpen, dismissOpen, spacer("codex.openActionsSpacer")};
    openStatus.children = {loading, openMessage, openActions};
    content.children.append(openStatus);
    content.children.append(codexHistoryLayout(s));
    auto conversation = codexConversationLayout(s);
    conversation.visible = !conversationPending && !s.history && !s.home;
    content.children.append(conversation);
    auto homeSpace = spacer("codex.homeSpacer");
    homeSpace.stretch = s.home ? 2 : 1;
    homeSpace.visible = !s.history && (s.home || (!loadingConversation && conversationPending));
    if (s.home && !s.history) {
        content.children.append(spacer("codex.homeMarkAbove"));
        auto markRow = control("codex.homeMarkRow", NativeControlKind::Row);
        auto mark = action("codex.homeMark", "", "Codex", false);
        mark.iconPath = "icons/codex-home.svg";
        mark.value = 48;
        markRow.children = {spacer("codex.homeMarkLeft"), mark, spacer("codex.homeMarkRight")};
        content.children.append(markRow);
    }
    content.children.append(homeSpace);
    auto goal = action("codex.goalStatus",
                       "$(target) " + s.goalLabel +
                           (s.goalExpanded ? " $(chevron-up)" : " $(chevron-down)"),
                       s.goalExpanded ? "Hide goal details" : "Show goal details");
    goal.visible = !conversationPending && !s.goalLabel.isEmpty() && !s.home && !s.history;
    content.children.append(goal);
    auto footer = control("codex.footer", NativeControlKind::Row);
    footer.contentsMargins = QMargins(0, 0, CodexContentInset, 0);
    auto account = action("codex.account", "$(device-desktop) Work locally",
                          s.accountLabel.isEmpty() ? "Account and sign-in" : s.accountLabel);
    account.children = codexAccountMenu(s, "codex.localMenu.");
    footer.visible = !conversationPending;
    footer.children = {account, spacer("codex.footerSpacer")};
    content.children.append(footer);
    root.children.append(content);
    auto locked = control("codex.locked", NativeControlKind::Row);
    locked.visible = !conversationPending && s.readOnly && !s.home && !s.history && !s.opening;
    locked.appearance = NativeControlAppearance::Composer;
    locked.margin = 12;
    auto lockMessage = control("codex.lockMessage", NativeControlKind::Label,
                               "This is open in another app\nClose it there to continue here.");
    lockMessage.stretch = 1;
    locked.children = {
        control("codex.lockIcon", NativeControlKind::Label, ""), lockMessage,
        action("codex.retryLocked", "Retry", "Retry opening this chat", s.ready && !s.busy)};
    root.children.append(locked);
    auto composer = codexComposerLayout(s);
    composer.visible = !conversationPending && !s.approval;
    root.children.append(codexApprovalLayout(s));
    root.children.append(composer);
    if (s.renaming) {
        auto dialog = control("codex.renameDialog", NativeControlKind::Dialog, "Rename chat");
        auto hint =
            control("codex.renameHint", NativeControlKind::Label, "Keep it short and recognizable");
        auto input = control("codex.renameInput", NativeControlKind::Input, s.renameDraft);
        input.accessibleName = "Chat name";
        auto buttons = control("codex.renameButtons", NativeControlKind::Row);
        auto cancel = action("codex.renameCancel", "Cancel", "Cancel rename");
        cancel.buttonStyle = NativeButtonStyle::Secondary;
        auto save = action("codex.renameSave", "Save", "Save chat name",
                           !s.renameDraft.trimmed().isEmpty());
        save.buttonStyle = NativeButtonStyle::Primary;
        buttons.children = {spacer("codex.renameSpacer"), cancel, save};
        dialog.children = {hint, input, buttons};
        root.children.append(dialog);
    }
    if (s.confirmingLogout)
        root.children.append(codexLogoutDialog(s));
    return root;
}
} // namespace litecode::codex
