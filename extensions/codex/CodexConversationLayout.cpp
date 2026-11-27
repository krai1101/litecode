#include "CodexViewControls.h"
namespace litecode::codex {
using namespace extensions;
using namespace view;
QString codexPromptText(const CodexTurnView& turn, const CodexViewState& state) {
    auto text = turn.prompt;
    // A transport/history newline is not an extra visible paragraph. Preserve
    // internal line breaks and indentation; copy/edit use the original session.
    while (text.endsWith('\n') || text.endsWith('\r'))
        text.chop(1);
    return text.size() <= 600 || state.expandedPrompts.contains(turn.key)
               ? text
               : text.left(600) + QString(QChar(0x2026));
}
NativeControl codexConversationLayout(const CodexViewState& s) {
    auto scroll = control("codex.conversation", NativeControlKind::Scroll);
    scroll.stretch = 1;
    scroll.appearance = NativeControlAppearance::Transcript;
    scroll.visible = !s.history;
    auto body = control("codex.messages", NativeControlKind::Column);
    body.spacing = 12;
    body.contentsMargins = QMargins(0, 0, 2 * CodexContentInset, 0);
    auto goalDetails = control("codex.goalObjective", NativeControlKind::Label, s.goalObjective);
    goalDetails.visible = !s.goalLabel.isEmpty() && s.goalExpanded;
    body.children.append(goalDetails);
    auto welcome = control("codex.welcome", NativeControlKind::Markdown,
                           "### Code with Codex\n\nAsk about this project, plan a change, or have "
                           "Codex work in your files.");
    welcome.visible =
        s.turns.isEmpty() && s.threads.isEmpty() && s.historyLoaded && s.historyError.isEmpty();
    body.children.append(welcome);
    for (int i = 0; i < s.turns.size(); ++i) {
        const auto& t = s.turns[i];
        const auto id = "codex.turn." + t.key;
        auto turn = control(id, NativeControlKind::Column);
        turn.spacing = 12;
        auto user = control(id + ".user", NativeControlKind::Column);
        user.visible = !codexPromptText(t, s).trimmed().isEmpty();
        user.margin = 14;
        user.appearance = NativeControlAppearance::Card;
        auto userActions = control(id + ".userActions", NativeControlKind::Row);
        userActions.appearance = NativeControlAppearance::HoverActions;
        auto userTime = control(id + ".userTime", NativeControlKind::Label, t.userTime);
        userTime.appearance = NativeControlAppearance::Muted;
        userTime.visible = !t.userTime.isEmpty();
        userActions.children = {
            spacer(id + ".userSpacer"), userTime,
            action(id + ".copyPrompt", s.copiedControlId == id + ".copyPrompt" ? "$(check)" : "$(copy)", "Copy message"),
            action(id + ".edit", "$(edit)", "Edit and resend this message", !s.busy && s.writable)};
        // User text may contain IDE-generated attachment/context headings. Keep
        // it literal; only assistant answers use Markdown formatting. Long
        // messages expand explicitly while copy/edit retain the original text.
        user.children = {control(id + ".prompt", NativeControlKind::Label, codexPromptText(t, s))};
        auto expandPrompt =
            action(id + ".expandPrompt",
                   s.expandedPrompts.contains(t.key) ? "Show less" : "Show full message",
                   "Expand or collapse this message");
        expandPrompt.visible = t.prompt.size() > 600;
        user.children.append(expandPrompt);
        auto userRow = control(id + ".userRow", NativeControlKind::Row);
        userRow.visible = user.visible;
        userRow.spacing = 0;
        auto inset = spacer(id + ".userInset");
        inset.stretch = 3;
        auto userSlot = control(id + ".userSlot", NativeControlKind::Row);
        userSlot.spacing = 0;
        userSlot.stretch = 7;
        // The outer row reserves 30% as leading space. Within the remaining
        // 70%, a natural-width card stays right-aligned instead of stretching
        // short prompts to the full message width.
        userSlot.children = {spacer(id + ".userAlignment"), user};
        userRow.children = {inset, userSlot};
        auto userGroup = control(id + ".userGroup", NativeControlKind::Column);
        userGroup.appearance = NativeControlAppearance::HoverGroup;
        userGroup.visible = user.visible || !t.images.isEmpty();
        userGroup.spacing = 4;
        auto images = control(id + ".images", NativeControlKind::Row);
        images.visible = !t.images.isEmpty();
        images.spacing = 8;
        images.children.append(spacer(id + ".imagesInset"));
        for (int image = 0; image < t.images.size(); ++image) {
            auto preview = control(id + ".image." + QString::number(image),
                                   NativeControlKind::Image, "Attached image");
            preview.iconPath = t.images[image];
            preview.value = 80;
            images.children.append(preview);
        }
        userGroup.children = {images, userRow, userActions};
        turn.children.append(userGroup);
        turn.children.append(codexWorkLayout(t, s));
        auto response = control(id + ".response", NativeControlKind::Markdown, t.response);
        response.visible = !t.response.isEmpty();
        auto responseGroup = control(id + ".responseGroup", NativeControlKind::Column);
        responseGroup.appearance = NativeControlAppearance::HoverGroup;
        responseGroup.spacing = 12;
        responseGroup.children.append(response);
        auto errorRow = control(id + ".errorNotice", NativeControlKind::Row);
        errorRow.visible = !t.error.isEmpty();
        errorRow.spacing = 12;
        errorRow.contentsMargins = QMargins(8, 8, 8, 8);
        auto errorIcon = action(id + ".errorIcon", "", "Turn error", false);
        errorIcon.iconPath = "icons/circle-alert.svg";
        errorIcon.iconSize = 12;
        errorIcon.appearance = NativeControlAppearance::Muted;
        auto error = control(id + ".error", NativeControlKind::Label, t.error);
        error.appearance = NativeControlAppearance::Muted;
        error.stretch = 1;
        errorRow.children = {errorIcon, error};
        responseGroup.children.append(errorRow);
        auto responseActions = control(id + ".responseActions", NativeControlKind::Row);
        responseActions.visible = !t.response.isEmpty() && !t.responseStreaming;
        responseActions.appearance = NativeControlAppearance::HoverActions;
        auto responseTime = control(id + ".responseTime", NativeControlKind::Label, t.responseTime);
        responseTime.appearance = NativeControlAppearance::Muted;
        responseTime.visible = !t.responseTime.isEmpty();
        responseActions.children = {action(id + ".copy", s.copiedControlId == id + ".copy" ? "$(check)" : "$(copy)", "Copy response"), responseTime,
                                    spacer(id + ".responseActionsSpacer")};
        responseGroup.children.append(codexChangesLayout(t, s, i == s.turns.size() - 1));
        responseGroup.children.append(responseActions);
        turn.children.append(responseGroup);
        body.children.append(turn);
    }
    body.children.append(spacer("codex.messagesSpacer"));
    scroll.children = {body};
    return scroll;
}
} // namespace litecode::codex
