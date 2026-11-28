#include "codex/ChatSession.h"
#include "TurnHistory.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace litecode::codex {
namespace {
QString settingsFile(const QString& storageDirectory) {
    const auto dir = storageDirectory.isEmpty()
                         ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                         : storageDirectory;
    QDir().mkpath(dir);
    return QDir(dir).filePath("codex-chat.ini");
}
QString projectKey(const QString& path) {
    return QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace
ChatSession::ChatSession(QObject* parent, QString storageDirectory)
    : QObject(parent), server_(this), storageDirectory_(std::move(storageDirectory)) {
    observationPool_.setMaxThreadCount(1);
    observationTimer_.setInterval(2000);
    connect(&observationTimer_, &QTimer::timeout, this, &ChatSession::refreshObservedThread);
    connect(this, &ChatSession::stateChanged, this, &ChatSession::updateObservation);
    connect(&server_, &AppServer::connected, this, [this] {
        emit errorCleared();
        refreshAccount();
        emit stateChanged();
    });
    connect(&server_, &AppServer::notification, this, &ChatSession::consume);
    connect(&server_, &AppServer::disconnected, this, [this](const QString& reason) {
        ++authGeneration_;
        ++accountGeneration_;
        loginPending_ = logoutPending_ = false;
        loginId_.clear();
        deviceCode_.clear();
        ++catalogGeneration_;
        ++epoch_;
        opening_ = false;
        threadReady_ = false;
        submitting_ = false;
        const bool turnFailure = !activeTurn_.isEmpty();
        if (turnFailure) {
            auto& turn = ensureTurn(activeTurn_);
            turn.status = "failed";
            turn.error.clear();
            turn.durationMs = QDateTime::currentMSecsSinceEpoch() - turn.startedMs;
            emit changed(turn.id);
        }
        activeTurn_.clear();
        emit error(reason);
        emit stateChanged();
    });
    connect(
        &server_, &AppServer::interaction, this,
        [this](const QJsonValue& id, const QString& method, const QJsonObject& params) {
            if (params.contains("threadId") && params.value("threadId").toString() != threadId_) {
                server_.reject(id, tr("Open the originating chat to respond to this interaction."));
                return;
            }
            emit interaction(id, method, params);
        });
}
ChatSession::~ChatSession() {
    observationCancelled_->store(true);
    observationPool_.waitForDone(); // No extension code may outlive its unloaded DLL.
}
void ChatSession::connectRuntime(const QString& executable) { server_.start(executable); }
void ChatSession::persistThread() {
    QSettings settings(settingsFile(storageDirectory_), QSettings::IniFormat);
    settings.setValue(projectKey(workspace_) + "/thread", threadId_);
}
void ChatSession::setWorkspace(const QString& path) {
    const auto clean = path.isEmpty() ? QString() : QDir(path).absolutePath();
    if (clean == workspace_)
        return;
    if (!activeTurn_.isEmpty())
        interrupt();
    if (!readOnly_ && goal_.value("status") == "active")
        server_.request("thread/goal/set", {{"threadId", threadId_}, {"status", "paused"}}, {});
    ++epoch_;
    turnsCursor_.clear();
    workspace_ = clean;
    reportedTurnErrors_.clear();
    threadId_.clear();
    threadWorkspace_.clear();
    title_.clear();
    activeTurn_.clear();
    turns_.clear();
    usage_ = {};
    goal_ = {};
    submitting_ = false;
    opening_ = false;
    readOnly_ = false;
    threadReady_ = false;
    QSettings settings(settingsFile(storageDirectory_), QSettings::IniFormat);
    threadId_ = settings.value(projectKey(workspace_) + "/thread").toString();
    emit reset();
    emit errorCleared();
    emit stateChanged();
    if (server_.ready() && !threadId_.isEmpty())
        resume(threadId_);
}
void ChatSession::newChat() {
    if (!readOnly_ && goal_.value("status") == "active") {
        emit error(tr("Pause or clear the active goal before starting another chat."));
        return;
    }
    if (busy()) {
        emit error(tr("Stop the active turn before starting another chat."));
        return;
    }
    ++epoch_;
    reportedTurnErrors_.clear();
    turnsCursor_.clear();
    threadId_.clear();
    threadWorkspace_.clear();
    title_.clear();
    turns_.clear();
    usage_ = {};
    goal_ = {};
    readOnly_ = false;
    opening_ = false;
    persistThread();
    emit errorCleared();
    emit reset();
    emit stateChanged();
}
void ChatSession::resume(const QString& id) {
    if (!readOnly_ && id != threadId_ && goal_.value("status") == "active") {
        emit error(tr("Pause or clear the active goal before switching chats."));
        return;
    }
    if (busy()) {
        emit error(tr("Stop the active turn before switching chats."));
        return;
    }
    const auto epoch = ++epoch_;
    emit errorCleared();
    submitting_ = true;
    opening_ = true;
    emit stateChanged();
    server_.request(
        "thread/resume",
        {{"threadId", id},
         {"cwd", workspace_},
         {"excludeTurns", true},
         {"initialTurnsPage",
          QJsonObject{{"limit", 8}, {"sortDirection", "desc"}, {"itemsView", "full"}}}},
        [this, epoch, id](const QJsonObject& result, const QString& failure) {
            if (epoch != epoch_)
                return;
            if (!failure.isEmpty()) {
                if (failure.contains("already has an active writer", Qt::CaseInsensitive))
                    readOwnedThread(id, epoch);
                else
                    failOpen(failure, epoch);
                return;
            }
            finishOpen(result.value("thread").toObject(),
                       result.value("initialTurnsPage").toObject(), false, epoch);
            emit modelSelectionChanged(result.value("model").toString(),
                                       result.value("reasoningEffort").toString());
        });
}
void ChatSession::loadOlderTurns() {
    if (turnsCursor_.isEmpty() || threadId_.isEmpty())
        return;
    const auto epoch = epoch_;
    server_.request("thread/turns/list",
                    {{"threadId", threadId_},
                     {"cursor", turnsCursor_},
                     {"limit", 8},
                     {"sortDirection", "desc"},
                     {"itemsView", "full"}},
                    [this, epoch](const QJsonObject& result, const QString& failure) {
                        if (epoch != epoch_)
                            return;
                        if (!failure.isEmpty()) {
                            emit error(failure);
                            return;
                        }
                        turnsCursor_ = result.value("nextCursor").toString();
                        auto recent = turns_;
                        turns_.clear();
                        const auto values = result.value("data").toArray();
                        for (auto it = values.end(); it != values.begin();) {
                            const auto object = (*--it).toObject();
                            auto& turn = ensureTurn(object.value("id").toString());
                            readTurnHistory(object, turn);
                        }
                        for (const auto& turn : recent) {
                            bool found = false;
                            for (const auto& older : turns_)
                                if (older.id == turn.id) {
                                    found = true;
                                    break;
                                }
                            if (!found)
                                turns_.append(turn);
                        }
                        emit changed({});
                        emit stateChanged();
                        emit olderTurnsLoaded();
                    });
}
void ChatSession::history(const QString& cursor) {
    // The chat list is account-wide and can finish while a conversation opens.
    const auto generation = ++historyGeneration_;
    QJsonObject params{{"limit", 30},
                       {"sortKey", "updated_at"},
                       {"sourceKinds", QJsonArray{"appServer", "cli", "vscode"}}};
    if (!cursor.isEmpty())
        params.insert("cursor", cursor);
    server_.request(
        "thread/list", params, [this, generation](const QJsonObject& r, const QString& e) {
            if (generation != historyGeneration_)
                return;
            if (!e.isEmpty()) {
                emit historyFailed(e);
                return;
            }
            emit historyReady(r.value("data").toArray(), r.value("nextCursor").toString());
        });
}
void ChatSession::rename(const QString& name) {
    if (!requireWritable())
        return;
    const auto id = threadId_;
    if (id.isEmpty() || name.trimmed().isEmpty())
        return;
    server_.request("thread/name/set", {{"threadId", id}, {"name", name.trimmed()}},
                    [this, id, name](const QJsonObject&, const QString& e) {
                        if (id != threadId_)
                            return;
                        if (!e.isEmpty()) {
                            emit error(e);
                            return;
                        }
                        title_ = name.trimmed();
                        emit stateChanged();
                    });
}
void ChatSession::archive() {
    if (!requireWritable())
        return;
    if (goal_.value("status") == "active") {
        emit error(tr("Pause or clear the goal before archiving this chat."));
        return;
    }
    if (threadId_.isEmpty() || busy())
        return;
    const auto id = threadId_;
    server_.request("thread/archive", {{"threadId", id}},
                    [this, id](const QJsonObject&, const QString& e) {
                        if (!e.isEmpty()) {
                            emit error(e);
                            return;
                        }
                        if (id == threadId_)
                            newChat();
                    });
}
void ChatSession::ensureThread(const QString& model, Completion done) {
    if (!threadId_.isEmpty()) {
        done();
        return;
    }
    const auto epoch = epoch_;
    auto params = codexApprovalPolicy(approvalMode_, true);
    params.insert("cwd", workspace_);
    if (!model.isEmpty())
        params.insert("model", model);
    server_.request("thread/start", params,
                    [this, epoch, done](const QJsonObject& r, const QString& e) {
                        if (epoch != epoch_)
                            return;
                        if (!e.isEmpty()) {
                            submitting_ = false;
                            emit error(e);
                            emit stateChanged();
                            return;
                        }
                        const auto thread = r.value("thread").toObject();
                        threadId_ = thread.value("id").toString();
                        threadReady_ = !threadId_.isEmpty();
                        title_ = thread.value("name").toString();
                        persistThread();
                        done();
                    });
}
void ChatSession::submit(const QJsonArray& input, const QString& model, const QString& effort,
                         bool goal) {
    if (authBusy() || (accountKnown_ && requiresAuth_ && account_.isEmpty())) {
        emit error(tr("Sign in to Codex before sending a message."));
        return;
    }
    if (!requireWritable())
        return;
    if (submitting_ || input.isEmpty())
        return;
    if (workspace_.isEmpty()) {
        emit error(tr("Open a project folder before asking Codex to work."));
        return;
    }
    if (!server_.ready()) {
        emit error(tr("Codex is connecting. Your draft is kept; retry when ready."));
        connectRuntime();
        return;
    }
    if (goal && !activeTurn_.isEmpty()) {
        emit error(tr("Stop the turn before setting a new goal."));
        return;
    }
    emit errorCleared();
    submitting_ = true;
    emit stateChanged();
    const auto epoch = epoch_;
    const auto serviceTier = serviceTier_;
    const auto policy = codexApprovalPolicy(approvalMode_, false);
    const auto mode = codexCollaborationMode(planMode_, model, effort);
    ensureThread(model, [this, epoch, input, model, effort, goal, serviceTier, policy, mode] {
        QJsonObject params{{"threadId", threadId_}, {"input", input}};
        QString method = "turn/start";
        if (!activeTurn_.isEmpty()) {
            method = "turn/steer";
            params.insert("expectedTurnId", activeTurn_);
        } else {
            if (!model.isEmpty())
                params.insert("model", model);
            if (!effort.isEmpty())
                params.insert("effort", effort);
            for (auto it = policy.begin(); it != policy.end(); ++it)
                params.insert(it.key(), it.value());
            params.insert("collaborationMode", mode);
            params.insert("serviceTier", serviceTier.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                               : QJsonValue(serviceTier));
        }
        // Goals are activated only after the objective is accepted as a user turn.
        // No client-side continuation loop is created.
        server_.request(
            method, params, [this, epoch, goal, input](const QJsonObject& r, const QString& e) {
                if (epoch != epoch_)
                    return;
                submitting_ = false;
                if (!e.isEmpty()) {
                    emit error(e);
                    emit stateChanged();
                    return;
                }
                const auto turn = r.value("turn").toObject();
                if (!turn.isEmpty()) {
                    auto& state = ensureTurn(turn.value("id").toString());
                    if (state.status.isEmpty())
                        state.status = "inProgress";
                    if (state.status == "inProgress")
                        activeTurn_ = state.id;
                }
                emit submissionAccepted();
                emit stateChanged();
                if (goal) {
                    const auto objective = input.first().toObject().value("text").toString();
                    const auto id = threadId_;
                    const auto goalTurn = turn.value("id").toString();
                    server_.request(
                        "thread/goal/set",
                        {{"threadId", id}, {"objective", objective}, {"status", "active"}},
                        [this, id, goalTurn](const QJsonObject& result, const QString& failure) {
                            if (id != threadId_)
                                return;
                            if (!failure.isEmpty())
                                emit error(failure);
                            else {
                                goal_ = result.value("goal").toObject();
                                if (!goalTurn.isEmpty()) {
                                    ensureTurn(goalTurn).sentAsGoal = true;
                                    QSettings settings(settingsFile(storageDirectory_),
                                                       QSettings::IniFormat);
                                    const auto key = projectKey(workspace_) + "/goalTurns";
                                    auto markers = settings.value(key).toStringList();
                                    if (!markers.contains(goalTurn))
                                        markers.append(goalTurn);
                                    settings.setValue(key, markers);
                                    emit changed(goalTurn);
                                }
                            }
                            emit stateChanged();
                        });
                }
            });
    });
}
void ChatSession::editAndSubmit(const QString& turnId, const QString& itemId, const QString& text,
                                const QString& model, const QString& effort) {
    if (!requireWritable())
        return;
    if (busy() || !server_.ready() || threadId_.isEmpty() || text.trimmed().isEmpty()) {
        emit error(tr("Wait for Codex to be idle and connected before editing a message."));
        return;
    }
    if (goal_.value("status") == "active") {
        emit error(tr("Pause or clear the goal before editing conversation history."));
        return;
    }
    QJsonArray input;
    bool found = false;
    int textIndex = -1;
    for (const auto& turn : turns_) {
        if (turn.id != turnId)
            continue;
        for (const auto& item : turn.items) {
            if (item.value("type") != "userMessage")
                continue;
            auto content = item.value("content").toArray();
            if (item.value("id") == itemId) {
                bool replaced = false;
                for (qsizetype i = 0; i < content.size(); ++i) {
                    auto value = content[i].toObject();
                    if (value.value("type") == "text" && !replaced) {
                        value.insert("text", text);
                        value.insert("text_elements", QJsonArray{});
                        content[i] = value;
                        textIndex = int(input.size() + i);
                        replaced = true;
                    }
                }
                if (!replaced) {
                    content.prepend(QJsonObject{
                        {"type", "text"}, {"text", text}, {"text_elements", QJsonArray{}}});
                    textIndex = int(input.size());
                }
                found = true;
            }
            for (const auto& value : content)
                input.append(value);
            if (found)
                break;
        }
        break;
    }
    if (!found) {
        emit error(tr("This message is no longer available. Reopen the chat and try again."));
        return;
    }
    const auto thread = threadId_;
    const auto epoch = ++epoch_;
    submitting_ = true;
    emit errorCleared();
    emit stateChanged();
    server_.request("thread/revert", {{"threadId", thread}, {"beforeTurnId", turnId}},
                    [this, epoch, thread, turnId, input, model, effort,
                     textIndex](const QJsonObject& result, const QString& failure) {
                        if (epoch != epoch_ || thread != threadId_)
                            return;
                        submitting_ = false;
                        if (!failure.isEmpty()) {
                            emit error(failure);
                            emit stateChanged();
                            return;
                        }
                        for (qsizetype i = 0; i < turns_.size(); ++i)
                            if (turns_[i].id == turnId) {
                                turns_.resize(i);
                                break;
                            }
                        reportedTurnErrors_.clear();
                        turnsCursor_ = result.value("turnsBackwardsCursor").toString();
                        emit revisionPrepared(input, textIndex);
                        emit reset();
                        submit(input, model, effort, false);
                    });
}
void ChatSession::interrupt() {
    if (readOnly_)
        return;
    if (activeTurn_.isEmpty())
        return;
    server_.request("turn/interrupt", {{"threadId", threadId_}, {"turnId", activeTurn_}},
                    [this](const QJsonObject&, const QString& e) {
                        if (!e.isEmpty())
                            emit error(e);
                    });
}
void ChatSession::compact() {
    if (!requireWritable())
        return;
    if (busy() || threadId_.isEmpty())
        return;
    submitting_ = true;
    emit stateChanged();
    const auto epoch = epoch_;
    server_.request("thread/compact/start", {{"threadId", threadId_}},
                    [this, epoch](const QJsonObject&, const QString& e) {
                        if (epoch != epoch_)
                            return;
                        submitting_ = false;
                        emit stateChanged();
                        if (!e.isEmpty())
                            emit error(e);
                    });
}
void ChatSession::updateGoal(const QString& status) {
    if (!requireWritable())
        return;
    server_.request("thread/goal/set", {{"threadId", threadId_}, {"status", status}},
                    [this](const QJsonObject&, const QString& e) {
                        if (!e.isEmpty())
                            emit error(e);
                    });
}
void ChatSession::clearGoal() {
    if (!requireWritable())
        return;
    server_.request("thread/goal/clear", {{"threadId", threadId_}},
                    [this](const QJsonObject&, const QString& e) {
                        if (!e.isEmpty())
                            emit error(e);
                    });
}
void ChatSession::refreshModels() {
    if (server_.ready())
        catalog({}, ++catalogGeneration_, {});
}
void ChatSession::catalog(const QString& cursor, quint64 generation, QJsonArray collected) {
    QJsonObject params{{"includeHidden", false}};
    if (!cursor.isEmpty())
        params.insert("cursor", cursor);
    server_.request("model/list", params,
                    [this, generation, collected](const QJsonObject& r, const QString& e) mutable {
                        if (generation != catalogGeneration_)
                            return;
                        if (!e.isEmpty()) {
                            emit error(e);
                            return;
                        }
                        for (const auto& model : r.value("data").toArray())
                            collected.append(model);
                        const auto next = r.value("nextCursor").toString();
                        if (!next.isEmpty())
                            catalog(next, generation, collected);
                        else {
                            models_ = collected;
                            emit catalogChanged();
                        }
                    });
}
TurnState& ChatSession::ensureTurn(const QString& id) {
    for (auto& turn : turns_)
        if (turn.id == id)
            return turn;
    turns_.append({id, {}, {}, QDateTime::currentMSecsSinceEpoch(), 0, {}, {}});
    return turns_.last();
}
void ChatSession::putItem(TurnState& turn, const QJsonObject& item) {
    for (auto& current : turn.items) {
        if (current.value("id") == item.value("id")) {
            current = item;
            return;
        }
    }
    turn.items.append(item);
}
void ChatSession::loadThread(const QJsonObject& thread) {
    observedRolloutPath_ = thread.value("path").toString();
    observedLifecycle_ = {};
    threadWorkspace_ = thread.value("cwd").toString();
    threadId_ = thread.value("id").toString();
    title_ = thread.value("name").toString();
    if (title_.isEmpty())
        title_ = thread.value("preview").toString().left(90);
    turns_.clear();
    activeTurn_.clear();
    for (const auto& value : thread.value("turns").toArray()) {
        const auto object = value.toObject();
        auto& turn = ensureTurn(object.value("id").toString());
        QSettings settings(settingsFile(storageDirectory_), QSettings::IniFormat);
        turn.sentAsGoal =
            settings.value(projectKey(workspace_) + "/goalTurns").toStringList().contains(turn.id);
        readTurnHistory(object, turn);
        if (!readOnly_ && turn.status == "inProgress")
            activeTurn_ = turn.id;
    }
    emit reset();
}
void ChatSession::consume(const QString& method, const QJsonObject& params) {
    if (method == "serverRequest/resolved") {
        emit interactionResolved(params.value("requestId"));
        return;
    }
    if (method == "account/updated" || method == "account/login/completed") {
        authNotification(method, params);
        return;
    }
    if (method == "account/rateLimits/updated") {
        limits_ = params;
        emit accountChanged();
        return;
    }
    if (params.value("threadId").toString() != threadId_ || threadId_.isEmpty())
        return;
    if (method == "thread/name/updated") {
        title_ = params.value("threadName").toString();
        emit stateChanged();
        return;
    }
    if (method == "thread/tokenUsage/updated") {
        usage_ = params.value("tokenUsage").toObject();
        emit stateChanged();
        return;
    }
    if (method == "thread/goal/updated") {
        goal_ = params.value("goal").toObject();
        emit stateChanged();
        return;
    }
    if (method == "thread/goal/cleared") {
        goal_ = {};
        emit stateChanged();
        return;
    }
    if (method == "error") {
        const auto turnId = params.value("turnId").toString();
        const auto message = params.value("error").toObject().value("message").toString();
        if (!turnId.isEmpty() && !params.value("willRetry").toBool() && !message.isEmpty()) {
            ensureTurn(turnId).error = message;
            emit changed(turnId);
        }
        if (!turnId.isEmpty()) {
            if (reportedTurnErrors_.value(turnId) != message) {
                reportedTurnErrors_.insert(turnId, message);
                emit error(message);
            }
        } else
            emit error(message);
        return;
    }
    const auto id =
        params.value("turnId").toString(params.value("turn").toObject().value("id").toString());
    if (id.isEmpty())
        return;
    auto& turn = ensureTurn(id);
    if (method == "turn/started") {
        activeTurn_ = id;
        turn.error.clear();
        turn.activeTools.clear();
        turn.status = "inProgress";
        const auto t = params.value("turn").toObject();
        if (t.value("startedAt").isDouble())
            turn.startedMs = t.value("startedAt").toInteger() * 1000;
        for (const auto& item : t.value("items").toArray())
            putItem(turn, item.toObject());
    } else if (method == "turn/completed") {
        turn.activeTools.clear();
        const auto t = params.value("turn").toObject();
        turn.status = t.value("status").toString();
        turn.durationMs =
            t.value("durationMs").toInteger(QDateTime::currentMSecsSinceEpoch() - turn.startedMs);
        if (turn.status == "completed")
            turn.error.clear();
        else {
            const auto message = t.value("error").toObject().value("message").toString();
            if (!message.isEmpty())
                turn.error = message;
            if (!message.isEmpty() && reportedTurnErrors_.value(id) != message) {
                reportedTurnErrors_.insert(id, message);
                emit error(message);
            }
        }
        for (const auto& item : t.value("items").toArray())
            putItem(turn, item.toObject());
        if (activeTurn_ == id)
            activeTurn_.clear();
        refreshAccount();
        const auto thread = threadId_;
        const auto previousTitle = title_;
        server_.request("thread/read", {{"threadId", thread}, {"includeTurns", false}},
                        [this, thread, previousTitle](const QJsonObject& result, const QString&) {
                            if (thread != threadId_)
                                return;
                            if (title_ != previousTitle)
                                return;
                            const auto metadata = result.value("thread").toObject();
                            auto title = metadata.value("name").toString();
                            if (title.isEmpty())
                                title = metadata.value("preview").toString().left(90);
                            if (!title.isEmpty()) {
                                title_ = title;
                                emit stateChanged();
                            }
                        });
    } else if (method == "item/started" || method == "item/completed") {
        const auto item = params.value("item").toObject();
        const auto itemId = item.value("id").toString();
        const auto type = item.value("type").toString();
        if (method == "item/started" && !itemId.isEmpty() && type != "agentMessage" &&
            type != "reasoning" && type != "userMessage")
            turn.activeTools.insert(itemId);
        else if (method == "item/completed")
            turn.activeTools.remove(itemId);
        if (method == "item/started" && item.value("type") == "fileChange")
            emit fileChangesProposed(threadId_, id, item.value("changes").toArray());
        putItem(turn, item);
    } else if (method == "turn/diff/updated")
        turn.diff = params.value("diff").toString();
    else if (method.endsWith("/delta") || method == "item/reasoning/summaryTextDelta" ||
             method == "item/reasoning/textDelta") {
        const auto itemId = params.value("itemId").toString();
        for (auto& item : turn.items) {
            if (item.value("id").toString() != itemId)
                continue;
            const auto field = method == "item/agentMessage/delta"             ? "text"
                               : method == "item/commandExecution/outputDelta" ? "aggregatedOutput"
                                                                               : "liveSummary";
            item.insert(field, item.value(field).toString() + params.value("delta").toString());
            break;
        }
    } else if (method == "item/commandExecution/outputDelta") {
        for (auto& item : turn.items)
            if (item.value("id") == params.value("itemId"))
                item.insert("aggregatedOutput", (item.value("aggregatedOutput").toString() +
                                                 params.value("delta").toString())
                                                    .right(1024 * 1024));
    } else
        return;
    emit changed(id);
    emit stateChanged();
}
} // namespace litecode::codex
