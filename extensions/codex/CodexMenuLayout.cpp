#include "CodexViewControls.h"
namespace litecode::codex {
using namespace extensions;
using namespace view;
namespace {
NativeControl item(const QString& id, const QString& text, const QString& icon,
                   bool enabled = true) {
    auto result = action("codex.menu." + id, text, text, enabled);
    result.iconPath = icon.startsWith("icons/") || icon == "codex.svg" ? icon
                                                                     : "icons/" + icon + ".svg";
    return result;
}
NativeControl separator(const QString& id) {
    return control("codex.menu." + id, NativeControlKind::Separator);
}
} // namespace
QVector<NativeControl> codexConversationMenu(const CodexViewState& state) {
    auto copy = item("copy", "Copy", "copy");
    copy.children = {item("copyDeeplink", "Copy deeplink", "copy"),
                     item("copyMarkdown", "Copy as Markdown", "copy"),
                     item("copyWorkingDirectory", "Copy working directory", "copy",
                          !state.workspace.isEmpty())};
    return {item("rename", "Rename", "pencil", state.writable && !state.busy),
            separator("renameDivider"), copy, separator("archiveDivider"),
            item("archive", "Archive", "archive", state.writable && !state.busy)};
}
QVector<NativeControl> codexAccountMenu(const CodexViewState& state, const QString& prefix) {
    auto identity = control("codex.menu.identity", NativeControlKind::Label, state.accountLabel);
    identity.iconPath = "icons/circle-user-round.svg";
    auto kind = control("codex.menu.accountKind", NativeControlKind::Label, state.accountKind);
    kind.iconPath = "icons/settings.svg";
    kind.visible = state.signedIn && !state.accountKind.isEmpty();
    QVector<NativeControl> result{identity, kind, separator("accountDivider"),
            item("settings", "Codex settings", "codex.svg"),
            item("shortcuts", "Keyboard shortcuts", "keyboard"),
            separator("signOutDivider"),
            item(state.signedIn ? "signOut" : state.signingIn ? "cancelSignIn" : "signIn",
                 state.signedIn ? "Log out" : state.signingIn ? "Cancel sign-in" : "Sign in",
                 "log-out", !state.busy && !state.authBusy)};
    for (auto& control : result)
        control.id.replace("codex.menu.", prefix);
    return result;
}
} // namespace litecode::codex
