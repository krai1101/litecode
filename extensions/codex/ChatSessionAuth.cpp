#include "ChatSession.h"
#include <utility>

namespace litecode::codex {
void ChatSession::refreshAccount() {
    const auto generation = ++accountGeneration_;
    server_.request("account/read", {{"refreshToken", false}},
        [this, generation](const QJsonObject& result, const QString& errorText) {
            if (generation != accountGeneration_)
                return;
            if (!errorText.isEmpty()) {
                emit error(errorText);
                return;
            }
            account_ = result.value("account").toObject();
            requiresAuth_ = result.value("requiresOpenaiAuth").toBool(true);
            accountKnown_ = true;
            emit accountChanged();
            if (!requiresAuth_ || !account_.isEmpty()) {
                refreshModels();
                if (!threadId_.isEmpty() && !opening_ && !threadReady_)
                    resume(threadId_);
            }
        });
    server_.request("account/rateLimits/read", {},
        [this, generation](const QJsonObject& result, const QString&) {
            if (generation == accountGeneration_) {
                limits_ = result;
                emit accountChanged();
            }
        });
}

void ChatSession::signIn() { startLogin({{"type", "chatgpt"}}); }
void ChatSession::signInWithDeviceCode() { startLogin({{"type", "chatgptDeviceCode"}}); }
void ChatSession::signInWithApiKey(QString key) {
    key = key.trimmed();
    if (key.isEmpty())
        return;
    startLogin({{"type", "apiKey"}, {"apiKey", std::move(key)}});
}

void ChatSession::startLogin(QJsonObject params) {
    if (!server_.ready() || busy() || authBusy() || signingIn())
        return;
    const auto generation = ++authGeneration_;
    ++accountGeneration_; // Older account reads cannot overwrite this transition.
    loginPending_ = true;
    deviceCode_.clear();
    emit errorCleared();
    emit stateChanged();
    // The key travels only in this request. It is never stored in chat/view state.
    server_.request("account/login/start", params,
        [this, generation](const QJsonObject& result, const QString& errorText) {
            if (generation != authGeneration_) {
                const auto cancelledId = result.value("loginId").toString();
                if (!cancelledId.isEmpty())
                    server_.request("account/login/cancel", {{"loginId", cancelledId}}, {});
                return;
            }
            loginPending_ = false;
            if (!errorText.isEmpty()) {
                emit error(errorText);
                emit stateChanged();
                return;
            }
            const auto type = result.value("type").toString();
            if (type == "apiKey") {
                refreshAccount();
            } else {
                loginId_ = result.value("loginId").toString();
                deviceCode_ = result.value("userCode").toString();
                const auto url = result.value(type == "chatgptDeviceCode"
                                                   ? "verificationUrl" : "authUrl").toString();
                if (loginId_.isEmpty() || url.isEmpty()) {
                    loginId_.clear();
                    deviceCode_.clear();
                    emit error(tr("Codex did not return a sign-in link. Please try again."));
                } else
                    emit loginUrl(url);
            }
            emit stateChanged();
        });
}

void ChatSession::cancelSignIn() {
    if (!signingIn())
        return;
    const auto id = std::exchange(loginId_, QString{});
    ++authGeneration_;
    loginPending_ = false;
    deviceCode_.clear();
    // A late login/start reply is cancelled by its callback once it supplies an ID.
    if (!id.isEmpty())
        server_.request("account/login/cancel", {{"loginId", id}}, {});
    emit errorCleared();
    emit stateChanged();
}

void ChatSession::signOut() {
    if (!server_.ready() || authBusy())
        return;
    if (busy()) {
        emit error(tr("Stop the turn before signing out."));
        return;
    }
    const auto generation = ++authGeneration_;
    ++accountGeneration_;
    logoutPending_ = true;
    emit errorCleared();
    emit stateChanged();
    server_.request("account/logout", {},
        [this, generation](const QJsonObject&, const QString& errorText) {
            if (generation != authGeneration_)
                return;
            logoutPending_ = false;
            if (!errorText.isEmpty())
                emit error(errorText);
            else {
                account_ = {};
                accountKnown_ = true;
                requiresAuth_ = true;
                loginId_.clear();
                deviceCode_.clear();
                limits_ = {};
                models_ = {};
                ++catalogGeneration_;
                ++historyGeneration_;
                ++epoch_;
                observationTimer_.stop();
                observationCancelled_->store(true);
                observationInFlight_ = false;
                opening_ = readOnly_ = threadReady_ = false;
                threadId_.clear();
                threadWorkspace_.clear();
                title_.clear();
                turns_.clear();
                turnsCursor_.clear();
                goal_ = usage_ = {};
                persistThread();
                emit reset();
                emit accountChanged();
                emit catalogChanged();
            }
            emit stateChanged();
        });
}

void ChatSession::authNotification(const QString& method, const QJsonObject& params) {
    if (method == "account/login/completed") {
        const auto completedId = params.value("loginId").toString();
        if (loginId_.isEmpty() || (!completedId.isEmpty() && completedId != loginId_))
            return; // Ignore completion of a cancelled or another client's login.
        ++authGeneration_;
        loginPending_ = false;
        loginId_.clear();
        deviceCode_.clear();
        if (!params.value("success").toBool())
            emit error(params.value("error").toString(tr("Sign-in did not complete.")));
        else
            emit errorCleared();
    }
    refreshAccount();
    emit stateChanged();
}
} // namespace litecode::codex
