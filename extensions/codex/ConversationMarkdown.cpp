#include "ConversationMarkdown.h"
#include "TurnHistory.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QPromise>
#include <QSet>
#include <stdexcept>
#include <memory>
namespace litecode::codex {
QString codexTurnMarkdown(const TurnState& turn) {
    QString result;
    for (const auto& item : turn.items) {
        const auto type = item.value("type").toString();
        if (type == "userMessage") {
            QString text;
            for (const auto& part : item.value("content").toArray()) {
                const auto content = part.toObject();
                if (content.value("type") == "text")
                    text += content.value("text").toString();
                else if (content.value("type") == "localImage")
                    text += "\n\n![Image attachment](<" +
                            QUrl::fromLocalFile(content.value("path").toString())
                                .toString(QUrl::FullyEncoded) + ">)";
                else if (content.value("type") == "image")
                    text += "\n\n[Image attachment]";
            }
            if (!text.trimmed().isEmpty())
                result += "## User\n\n" + text.trimmed() + "\n\n";
        } else if (type == "agentMessage" && !item.value("text").toString().trimmed().isEmpty())
            result += "## Assistant\n\n" + item.value("text").toString().trimmed() + "\n\n";
    }
    return result;
}
namespace {
struct Export final : std::enable_shared_from_this<Export> {
    QPointer<AppServer> server;
    QString thread, text;
    QPromise<QString> promise;
    QSet<QString> cursors, turns;
    void fail(const QString& reason) {
        promise.setException(std::make_exception_ptr(std::runtime_error(reason.toStdString())));
        promise.finish();
    }
    void page(const QString& cursor = {}) {
        if (!server || !server->ready()) {
            fail("Codex is not connected.");
            return;
        }
        QJsonObject params{{"threadId", thread}, {"limit", 8},
                           {"sortDirection", "asc"}, {"itemsView", "full"}};
        if (!cursor.isEmpty()) params.insert("cursor", cursor);
        server->request("thread/turns/list", params,
            [self = shared_from_this()](const QJsonObject& page, const QString& error) {
                if (!error.isEmpty()) { self->fail(error); return; }
                if (!page.value("data").isArray()) {
                    self->fail("Codex returned invalid conversation history.");
                    return;
                }
                for (const auto& value : page.value("data").toArray()) {
                    TurnState turn;
                    readTurnHistory(value.toObject(), turn);
                    if (self->turns.contains(turn.id)) continue;
                    self->turns.insert(turn.id);
                    self->text += codexTurnMarkdown(turn);
                }
                // Leave room for the clipboard request's JSON envelope. Report
                // an oversized export rather than truncating or breaking IPC.
                if (QJsonDocument(QJsonObject{{"text", self->text}})
                        .toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024) {
                    self->fail("This conversation is too large to copy to the clipboard.");
                    return;
                }
                const auto next = page.value("nextCursor").toString();
                if (!next.isEmpty()) {
                    if (self->cursors.contains(next)) {
                        self->fail("Codex returned a repeated history cursor.");
                        return;
                    }
                    self->cursors.insert(next);
                    self->page(next);
                } else {
                    self->promise.addResult(self->text.trimmed() + '\n');
                    self->promise.finish();
                }
            });
    }
};
} // namespace
QFuture<QString> exportCodexMarkdown(AppServer& server, const QString& threadId,
                                    const QString& title) {
    auto exportState = std::make_shared<Export>();
    exportState->server = &server;
    exportState->thread = threadId;
    exportState->text = "# " + (title.isEmpty() ? "Codex chat" : title.simplified()) + "\n\n";
    exportState->promise.start();
    auto future = exportState->promise.future();
    exportState->page();
    return future;
}
} // namespace litecode::codex
