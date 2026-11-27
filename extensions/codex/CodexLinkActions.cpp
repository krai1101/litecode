#include "CodexViewProvider.h"
#include <utility>
namespace litecode::codex {
void CodexViewProvider::receiveThreadLink(const QString& id) {
    if (session_.busy() && !session_.opening()) {
        notice("Stop the active turn before opening another chat.");
        return;
    }
    pendingThreadLink_ = id;
    if (view_)
        view_->show(false);
    else
        context_.commands().executeCommand("litecode.codex.view.focus")
            .onFailed(this, [this](const std::exception& error) {
                pendingThreadLink_.clear();
                notice(QString::fromUtf8(error.what()));
                return QJsonValue{};
            });
    openPendingThreadLink();
}
void CodexViewProvider::openPendingThreadLink() {
    if (!pendingThreadLink_.isEmpty() && view_ && session_.server().ready() &&
        session_.accountKnown() && (!session_.requiresAuth() || !session_.account().isEmpty()) &&
        !session_.busy())
        openThread(std::exchange(pendingThreadLink_, QString{}));
}
} // namespace litecode::codex
