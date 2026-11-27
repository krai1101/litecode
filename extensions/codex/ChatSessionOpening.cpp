#include "codex/ChatSession.h"

namespace litecode::codex {
bool ChatSession::requireWritable() {
    if (canWrite())
        return true;
    emit error(readOnly_
                   ? tr("This chat is read-only because another Codex session owns it. "
                        "Close it there, then retry opening it here, or start a new chat.")
                   : tr("Open this chat successfully before continuing, or start a new chat."));
    return false;
}

void ChatSession::failOpen(const QString& message, quint64 epoch) {
    if (epoch != epoch_ || !opening_)
        return;
    ++epoch_; // Ignore any later reply from this failed opening attempt.
    opening_ = false;
    submitting_ = false;
    emit threadOpenFailed(tr("Couldn't open this chat. %1").arg(message));
    emit stateChanged();
}

void ChatSession::finishOpen(QJsonObject thread, const QJsonObject& page, bool readOnly,
                             quint64 epoch) {
    if (epoch != epoch_)
        return;
    if (thread.value("id").toString().isEmpty()) {
        failOpen(tr("Codex returned an invalid chat."), epoch);
        return;
    }
    turnsCursor_ = page.value("nextCursor").toString();
    if (!page.isEmpty()) {
        QJsonArray turns;
        const auto values = page.value("data").toArray();
        for (auto it = values.end(); it != values.begin();)
            turns.append(*--it);
        thread.insert("turns", turns);
    }
    readOnly_ = readOnly;
    threadReady_ = !readOnly;
    opening_ = false;
    submitting_ = false;
    usage_ = {};
    goal_ = {};
    loadThread(thread);
    persistThread();
    emit errorCleared();
    emit threadOpened();
    emit stateChanged();
    const auto current = threadId_;
    server_.request("thread/goal/get", {{"threadId", current}},
                    [this, epoch, current](const QJsonObject& r, const QString&) {
                        if (epoch != epoch_ || current != threadId_)
                            return;
                        goal_ = r.value("goal").toObject();
                        emit stateChanged();
                    });
}

void ChatSession::cancelOpen() {
    if (!opening_)
        return;
    ++epoch_;
    opening_ = false;
    submitting_ = false;
    emit stateChanged();
}

void ChatSession::readOwnedThread(const QString& id, quint64 epoch) {
    // Viewing history must not compete with another editor for the writer lock.
    // Both read calls are paginated and leave the owning process untouched.
    server_.request(
        "thread/read", {{"threadId", id}, {"includeTurns", false}},
        [this, id, epoch](const QJsonObject& result, const QString& failure) {
            if (epoch != epoch_)
                return;
            if (!failure.isEmpty()) {
                failOpen(failure, epoch);
                return;
            }
            const auto thread = result.value("thread").toObject();
            server_.request(
                "thread/turns/list",
                {{"threadId", id}, {"limit", 8}, {"sortDirection", "desc"}, {"itemsView", "full"}},
                [this, thread, epoch](const QJsonObject& page, const QString& error) {
                    if (epoch != epoch_)
                        return;
                    if (!error.isEmpty())
                        failOpen(error, epoch);
                    else
                        readObservedPage(page, thread.value("path").toString(), epoch,
                                         [this, thread, epoch](const QJsonObject& observed) {
                                             finishOpen(thread, observed, true, epoch);
                                         });
                });
        });
}
} // namespace litecode::codex
