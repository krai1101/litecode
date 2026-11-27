#include "CodexViewProvider.h"
#include <utility>

namespace litecode::codex {
bool CodexViewProvider::handleAuthAction(const extensions::NativeViewEvent& event) {
    if (!event.controlId.startsWith("codex.auth."))
        return false;
    const auto id = event.controlId.mid(11);
    if (id == "key" && state_.apiKeyEntry && !session_.authBusy() &&
        (event.type == "textChanged" || event.type == "submitted")) {
        apiKeyDraft_ = event.text;
        view_->setEnabled("codex.auth.keySubmit", !apiKeyDraft_.trimmed().isEmpty());
        if (event.type != "submitted")
            return true;
    } else if (event.type == "dismissed" && id == "logoutDialog") {
        state_.confirmingLogout = false;
        render();
        return true;
    } else if (event.type != "clicked")
        return true;

    if (id == "logoutCancel")
        state_.confirmingLogout = false;
    else if (id == "logoutConfirm" && state_.confirmingLogout && !session_.authBusy()) {
        state_.confirmingLogout = false;
        session_.signOut();
    } else if (id == "chatgpt")
        session_.signIn();
    else if (id == "device")
        session_.signInWithDeviceCode();
    else if (id == "cancel")
        session_.cancelSignIn();
    else if (id == "apiKey") {
        apiKeyDraft_.clear();
        state_.apiKeyEntry = true;
    } else if (id == "keyCancel" && !session_.authBusy()) {
        apiKeyDraft_.clear();
        view_->setText("codex.auth.key", {});
        state_.apiKeyEntry = false;
    } else if ((id == "keySubmit" || id == "key") && state_.apiKeyEntry &&
               !session_.authBusy() && !apiKeyDraft_.trimmed().isEmpty()) {
        auto key = std::exchange(apiKeyDraft_, QString{});
        view_->setText("codex.auth.key", {});
        session_.signInWithApiKey(std::move(key));
    } else if (id == "getKey")
        context_.env().openExternal(QUrl("https://platform.openai.com/api-keys"));
    render();
    return true;
}
} // namespace litecode::codex
