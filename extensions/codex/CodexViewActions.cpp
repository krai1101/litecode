#include "CodexDiffDocuments.h"
#include "CodexHistoryWindow.h"
#include "CodexTurnActions.h"
#include "CodexTurnPresentation.h"
#include "CodexViewProvider.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>
namespace litecode::codex {
void CodexViewProvider::openThread(const QString& id) {
    if (id.isEmpty() || session_.busy())
        return;
    lastOpenThread_ = id;
    followOpenedThread_ = true;
    state_.openError.clear();
    state_.notice.clear();
    session_.resume(id);
    if (session_.opening()) {
        state_.home = false;
        state_.history = false;
    }
    render();
}
void CodexViewProvider::loadHistory(const QString& cursor) {
    if (session_.accountKnown() && session_.requiresAuth() && session_.account().isEmpty())
        return;
    state_.historyLoading = true;
    state_.historyMore = !cursor.isEmpty();
    state_.historyError.clear();
    session_.history(cursor);
}
bool CodexViewProvider::handlePresentationAction(const extensions::NativeViewEvent& event) {
    const auto& id = event.controlId;
    if (id == "codex.retryOpen" || id == "codex.retryLocked") {
        openThread(lastOpenThread_.isEmpty() ? session_.threadId() : lastOpenThread_);
        return true;
    }
    if (id == "codex.dismissOpen") {
        state_.openError.clear();
        state_.home = true;
        state_.history = false;
        followOpenedThread_ = false;
        render();
        return true;
    }
    if (id == "codex.goalStatus") {
        state_.goalExpanded = !state_.goalExpanded;
        render();
        return true;
    }
    if (id == "codex.back") {
        pendingThreadLink_.clear();
        pendingOlderEndId_.clear();
        if (session_.opening()) {
            followOpenedThread_ = false;
            session_.cancelOpen();
        }
        state_.openError.clear();
        notice({});
        if (state_.history) {
            state_.history = false;
            state_.home = !historyReturnToConversation_;
        } else {
            state_.home = true;
            if (!state_.historyLoading)
                loadHistory();
        }
        render();
        return true;
    }
    if (id == "codex.historySearch") {
        state_.historyFilter = event.text;
        historyFilterTimer_.start();
        return true;
    }
    if (id == "codex.retryHistory") {
        if (!state_.historyLoading) {
            loadHistory();
            render();
        }
        return true;
    }
    if (id == "codex.viewAllHistory") {
        if (!session_.busy()) {
            historyReturnToConversation_ = false;
            state_.history = true;
            state_.historyFilter.clear();
            loadHistory();
            render();
        }
        return true;
    }
    if (id == "codex.moreHistory") {
        if (!state_.historyLoading && !state_.historyCursor.isEmpty()) {
            loadHistory(state_.historyCursor);
            render();
        }
        return true;
    }
    if (id == "codex.recentThreads" && event.index >= 0 && event.index < 3 &&
        event.index < state_.threads.size() && !session_.busy()) {
        openThread(state_.threads[event.index].toObject().value("id").toString());
        return true;
    }
    if (event.type == "linkActivated") {
        const QUrl uri(event.text);
        if (uri.isLocalFile())
            context_.openFile(uri.toLocalFile());
        else if (uri.scheme() == "https" || uri.scheme() == "http")
            context_.env().openExternal(uri);
        return true;
    }
    if (id == "codex.conversation") {
        if (event.type == "scrollStart" && state_.hasOlder && !olderRequestInFlight_ &&
            !state_.turns.isEmpty()) {
            const auto& turns = session_.turns();
            for (int i = 0; i < turns.size(); ++i)
                if (turns[i].id == state_.turns.first().turnId) {
                    if (i > 0) {
                        prependCodexHistoryWindow(state_, turns, turns[i].id);
                        render();
                    } else if (session_.hasOlderTurns()) {
                        pendingOlderEndId_ = turns[i].id;
                        olderRequestInFlight_ = true;
                        session_.loadOlderTurns();
                    }
                    break;
                }
        } else if (event.type == "scrollEnd" && !state_.turnWindowEndId.isEmpty()) {
            appendCodexHistoryWindow(state_, session_.turns());
            render();
        } else if (event.type == "scrollToEnd") {
            pendingOlderEndId_.clear();
            const bool changedWindow = !state_.turnWindowEndId.isEmpty();
            state_.turnWindowEndId.clear();
            if (changedWindow)
                render();
        }
        return true;
    }
    if (id == "codex.manageAttachments") {
        QVector<extensions::QuickPickItem> items;
        for (const auto& path : state_.attachments) {
            extensions::QuickPickItem item;
            item.label = QFileInfo(path).fileName();
            item.description = path;
            items.append(item);
        }
        extensions::QuickPickOptions options;
        options.title = "Remove an attachment";
        context_.window()
            .showQuickPick(items, options)
            .then(this,
                  [this](const std::optional<
                         extensions::QuickPickSelection<extensions::QuickPickItem>>& selection) {
                      if (!selection || session_.busy())
                          return;
                      const auto* picked = std::get_if<extensions::QuickPickItem>(&*selection);
                      if (picked && picked->description) {
                          state_.attachments.removeAll(*picked->description);
                          render();
                      }
                  });
        return true;
    }
    if (id == "codex.cancelEdit") {
        if (state_.editing) {
            state_.draft = savedDraft_;
            savedDraft_.clear();
            editTurn_.clear();
            editItem_.clear();
            state_.editing = false;
            context_.workspaceState().update("draft", state_.draft);
            render();
        }
        return true;
    }
    if (id.startsWith("codex.attachment.") && id.endsWith(".remove")) {
        bool valid{};
        const int index = id.section('.', 2, 2).toInt(&valid);
        if (valid && !session_.busy() && index >= 0 && index < state_.attachments.size()) {
            state_.attachments.removeAt(index);
            render();
        }
        return true;
    }
    if (id.startsWith("codex.undo.")) {
        turnActions_->undoLast();
        return true;
    }
    if (!id.startsWith("codex.turn."))
        return false;
    const auto key = id.section('.', 2, 2), action = id.section('.', 3);
    const auto found = std::find_if(state_.turns.cbegin(), state_.turns.cend(),
                                    [&](const auto& turn) { return turn.key == key; });
    if (found == state_.turns.cend())
        return true;
    QString originalPrompt, originalResponse, originalDiff;
    if (action == "edit" || action == "copy" || action == "copyPrompt" || action == "review" ||
        action.startsWith("reviewFile.")) {
        for (const auto& turn : session_.turns()) {
            if (turn.id != found->turnId)
                continue;
            originalDiff = codexTurnDiff(turn);
            originalResponse = codexFinalResponse(turn);
            for (const auto& item : turn.items) {
                if (item.value("type") == "userMessage" &&
                    item.value("id").toString() == found->userItemId)
                    for (const auto& part : item.value("content").toArray())
                        originalPrompt += part.toObject().value("text").toString();
            }
            break;
        }
    }
    if (action == "expandPrompt") {
        if (state_.expandedPrompts.contains(key))
            state_.expandedPrompts.remove(key);
        else
            state_.expandedPrompts.insert(key);
        render();
    } else if (action == "copy" || action == "copyPrompt") {
        context_.env()
            .clipboard()
            .writeText(action == "copy" ? originalResponse : originalPrompt)
            .then(this, [this, id, thread = session_.threadId()] {
                if (session_.threadId() != thread)
                    return;
                state_.copiedControlId = id;
                copyFeedbackTimer_.start();
                render();
            })
            .onFailed(this, [this](const std::exception& error) {
                notice(QString::fromUtf8(error.what()));
            });
    } else if (action.startsWith("activityGroup.")) {
        const auto groupKey = key + "/activity." + action.section('.', 1);
        if (state_.expandedActivity.contains(groupKey))
            state_.expandedActivity.remove(groupKey);
        else
            state_.expandedActivity.insert(groupKey);
        render();
    } else if (action.startsWith("tool.")) {
        const auto toolKey = key + "/" + action.section('.', 1);
        if (state_.expandedActivity.contains(toolKey))
            state_.expandedActivity.remove(toolKey);
        else
            state_.expandedActivity.insert(toolKey);
        render();
    } else if (action == "moreFiles") {
        if (state_.expandedChanges.contains(key))
            state_.expandedChanges.remove(key);
        else
            state_.expandedChanges.insert(key);
        render();
    } else if (action == "tools") {
        const bool expanded = codexWorkExpanded(*found, state_);
        if (expanded) {
            state_.expandedActivity.remove(key);
            state_.expandedActivity.insert(key + "/collapsed");
        } else {
            state_.expandedActivity.remove(key + "/collapsed");
            state_.expandedActivity.insert(key);
        }
        render();
    } else if (action == "review" || action.startsWith("reviewFile.")) {
        QString selected;
        QStringList requestedPaths;
        if (action.startsWith("reviewFile.")) {
            bool valid = false;
            const int index = action.section('.', 1).toInt(&valid);
            if (!valid || index < 0 || index >= found->files.size())
                return true;
            selected = QDir::cleanPath(
                QDir(session_.threadWorkspace()).absoluteFilePath(found->files[index].path));
            requestedPaths.append(found->files[index].path);
        } else {
            for (const auto& file : found->files)
                requestedPaths.append(file.path);
        }
        const auto snapshots = turnActions_->reviewSnapshots(found->turnId, originalDiff, requestedPaths);
        bool opened = false;
        for (const auto& file : snapshots) {
#ifdef Q_OS_WIN
            constexpr auto pathSensitivity = Qt::CaseInsensitive;
#else
            constexpr auto pathSensitivity = Qt::CaseSensitive;
#endif
            if (!selected.isEmpty() &&
                QDir::cleanPath(QDir::fromNativeSeparators(file.path)).compare(
                    QDir::fromNativeSeparators(selected), pathSensitivity) != 0)
                continue;
            const auto left = diffDocuments_->add(found->turnId, file.path, true, file.before);
            const auto right = diffDocuments_->add(found->turnId, file.path, false, file.after);
            context_.commands()
                .executeCommand("vscode.diff",
                                {left.toString(QUrl::FullyEncoded),
                                 right.toString(QUrl::FullyEncoded),
                                 QFileInfo(file.path).fileName() + " (Codex Changes)"})
                .onFailed(this, [this](const std::exception& error) {
                    notice(QString::fromUtf8(error.what()));
                    return QJsonValue{};
                });
            opened = true;
        }
        if (!opened)
            notice("The before/after snapshots for this change are unavailable. No files were "
                   "modified.");
    } else if (action == "edit" && session_.canWrite() && !session_.busy() &&
               !found->userItemId.isEmpty()) {
        if (!state_.editing)
            savedDraft_ = state_.draft;
        editTurn_ = found->turnId;
        editItem_ = found->userItemId;
        state_.draft = originalPrompt;
        state_.editing = true;
        context_.workspaceState().update("draft", state_.draft);
        render();
    }
    return true;
}
} // namespace litecode::codex
