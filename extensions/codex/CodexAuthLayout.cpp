#include "CodexViewControls.h"

namespace litecode::codex {
using namespace extensions;
using namespace view;
bool codexSignedOut(const CodexViewState& s) {
    return s.accountKnown && s.requiresAuth && !s.signedIn;
}

NativeControl codexLogoutDialog(const CodexViewState& s) {
    auto dialog = control("codex.auth.logoutDialog", NativeControlKind::Dialog, "Log out?");
    auto message = control("codex.auth.logoutMessage", NativeControlKind::Label,
                           "You\u2019ll need to sign in again to keep using Codex");
    auto buttons = control("codex.auth.logoutButtons", NativeControlKind::Row);
    auto cancel = action("codex.auth.logoutCancel", "Cancel", "Cancel", !s.authBusy);
    auto confirm = action("codex.auth.logoutConfirm", "Log out", "Log out", !s.authBusy);
    confirm.buttonStyle = NativeButtonStyle::Danger;
    buttons.children = {spacer("codex.auth.logoutSpace"), cancel, confirm};
    dialog.children = {message, buttons};
    return dialog;
}

NativeControl codexAuthLayout(const CodexViewState& s) {
    auto root = control("codex.auth.root", NativeControlKind::Column);
    root.appearance = NativeControlAppearance::Transcript;
    root.margin = 24;
    root.spacing = 12;
    auto markRow = control("codex.auth.markRow", NativeControlKind::Row);
    auto mark = action("codex.auth.mark", "", "Codex", false);
    mark.iconPath = "icons/codex-wordmark.svg";
    mark.value = 180;
    mark.appearance = NativeControlAppearance::Plain;
    markRow.children = {spacer("codex.auth.markLeft"), mark, spacer("codex.auth.markRight")};
    auto above = spacer("codex.auth.above");
    auto below = spacer("codex.auth.below");
    below.stretch = 2;
    root.children = {above, markRow, below};
    auto notice = control("codex.notice", NativeControlKind::Label, s.notice);
    notice.appearance = NativeControlAppearance::Muted;
    notice.visible = !s.notice.isEmpty();
    root.children.append(notice);

    if (s.apiKeyEntry) {
        auto card = control("codex.auth.keyCard", NativeControlKind::Column);
        card.appearance = NativeControlAppearance::Surface;
        card.margin = 18;
        card.spacing = 12;
        auto input = control("codex.auth.key", NativeControlKind::Input);
        input.password = true;
        input.placeholder = "sk-...";
        input.accessibleName = "OpenAI API key";
        input.enabled = !s.authBusy;
        auto hint = control("codex.auth.keyHint", NativeControlKind::Label,
                            "Cloud chats disabled with API key");
        hint.appearance = NativeControlAppearance::Muted;
        auto buttons = control("codex.auth.keyButtons", NativeControlKind::Row);
        auto cancel = action("codex.auth.keyCancel", "Cancel", "Cancel", !s.authBusy);
        auto save = action("codex.auth.keySubmit", "OK", "Sign in with API key", false);
        save.buttonStyle = NativeButtonStyle::Primary;
        buttons.children = {action("codex.auth.getKey", "Get API Key $(link-external)", "Get API Key"),
                            spacer("codex.auth.keySpace"), cancel, save};
        card.children = {control("codex.auth.keyTitle", NativeControlKind::Label,
                                 "Enter your OpenAI API key"), input, hint, buttons};
        root.children.append(card);
    } else if (s.signingIn) {
        auto spinner = control("codex.auth.spinnerRow", NativeControlKind::Row);
        spinner.children = {spacer("codex.auth.spinnerLeft"),
                            action("codex.auth.spinner", "$(loading~spin)", "Signing in", false),
                            spacer("codex.auth.spinnerRight")};
        root.children.append(spinner);
        if (!s.deviceCode.isEmpty())
            root.children.append(control("codex.auth.code", NativeControlKind::Label,
                                          "Enter this code in your browser: " + s.deviceCode));
        auto row = control("codex.auth.cancelRow", NativeControlKind::Row);
        row.children = {spacer("codex.auth.cancelLeft"),
                        action("codex.auth.cancel", "Cancel", "Cancel sign-in", s.ready),
                        spacer("codex.auth.cancelRight")};
        root.children.append(row);
    } else {
        auto chatgpt = action("codex.auth.chatgpt", "Sign in with ChatGPT", "Sign in with ChatGPT",
                               s.ready && !s.authBusy);
        chatgpt.buttonStyle = NativeButtonStyle::Primary;
        auto apiKey = action("codex.auth.apiKey", "Use API Key", "Use API Key", s.ready && !s.authBusy);
        apiKey.buttonStyle = NativeButtonStyle::Secondary;
        auto more = action("codex.auth.more", "More options", "More sign-in options", s.ready);
        more.children = {action("codex.auth.device", "Sign in with a device code", "Use a device code")};
        auto choices = control("codex.auth.choices", NativeControlKind::Column);
        choices.spacing = 8;
        choices.children = {chatgpt, apiKey, more};
        auto row = control("codex.auth.choicesRow", NativeControlKind::Row);
        row.children = {spacer("codex.auth.choicesLeft"), choices,
                        spacer("codex.auth.choicesRight")};
        root.children.append(row);
    }
    return root;
}
} // namespace litecode::codex
