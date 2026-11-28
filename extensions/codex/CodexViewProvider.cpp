#include "CodexViewProvider.h"
#include "CodexApproval.h"
#include "CodexDiffDocuments.h"
#include "CodexHistoryWindow.h"
#include "CodexTurnActions.h"
#include "CodexTurnPresentation.h"
#include "CodexViewIcons.h"
#include "CodexThreadLink.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QPointer>
#include <QStandardPaths>
namespace litecode::codex {
using namespace extensions;
CodexViewProvider::CodexViewProvider(ExtensionContext& context)
    : context_(context), session_(this, context.globalStorageUri().toLocalFile()),
      attachmentStore_(QDir(context.globalStorageUri().toLocalFile()).filePath("image-cache")) {
    const QPointer<CodexViewProvider> self(this);
    subscriptions_.push_back(context_.window().registerUriHandler(
        std::make_shared<CodexThreadLinkHandler>(context_.env().uriScheme(), context_.extensionId(),
            [self](const QString& id) { if (self) self->receiveThreadLink(id); })));
    diffDocuments_ = std::make_shared<CodexDiffDocuments>();
    subscriptions_.push_back(
        context_.workspace().registerTextDocumentContentProvider("codex-diff", diffDocuments_));
    resetInteractions();
    turnActions_ = std::make_unique<CodexTurnActions>(
        context_, session_, [this](const QString& text) { notice(text); }, this);
    state_.planMode = context.workspaceState().get("planMode", false).toBool();
    state_.fastMode = context.globalState().get("fastMode", false).toBool();
    state_.draft = context.workspaceState().get("draft", "").toString();
    state_.model = context.globalState().get("model", "").toString();
    state_.effort = context.globalState().get("effort", "medium").toString();
    refreshTimer_.setSingleShot(true);
    copyFeedbackTimer_.setSingleShot(true);
    copyFeedbackTimer_.setInterval(1500);
    connect(&copyFeedbackTimer_, &QTimer::timeout, this, [this] {
        state_.copiedControlId.clear();
        render();
    });
    refreshTimer_.setInterval(33);
    connect(&refreshTimer_, &QTimer::timeout, this, &CodexViewProvider::refresh);
    workClock_.setInterval(1000);
    connect(&workClock_, &QTimer::timeout, this, [this] {
        if (view_ && view_->visible() &&
            (session_.busy() || (session_.readOnly() && session_.hasRunningTurn())))
            refresh();
    });
    workClock_.start();
    historyFilterTimer_.setSingleShot(true);
    historyFilterTimer_.setInterval(120);
    connect(&historyFilterTimer_, &QTimer::timeout, this, &CodexViewProvider::render);
    connect(&session_, &ChatSession::changed, this, [this] {
        if (!refreshTimer_.isActive())
            refreshTimer_.start();
    });
    connect(&session_, &ChatSession::stateChanged, this, [this] {
        if (!session_.busy() && !approvals_.isEmpty())
            resetInteractions();
        if (!refreshTimer_.isActive())
            refreshTimer_.start();
    });
    connect(&session_, &ChatSession::reset, this, [this] {
        copyFeedbackTimer_.stop();
        state_.copiedControlId.clear();
        if (session_.threadId().isEmpty())
            state_.home = true;
        resetInteractions();
        editTurn_.clear();
        editItem_.clear();
        savedDraft_.clear();
        state_.editing = false;
        state_.renaming = false;
        state_.renameDraft.clear();
        renameThread_.clear();
        state_.turnWindowEndId.clear();
        state_.historyWindowSize = 8;
        state_.expandedActivity.clear();
        state_.expandedPrompts.clear();
        state_.expandedChanges.clear();
        state_.goalExpanded = false;
        pendingOlderEndId_.clear();
        olderRequestInFlight_ = false;
        render();
    });
    connect(&session_, &ChatSession::error, this, [this](const QString& error) {
        pendingOlderEndId_.clear();
        olderRequestInFlight_ = false;
        // Turn failures belong beside that turn's response and file changes.
        // Connection, navigation and retry notices still use the general area.
        for (const auto& turn : session_.turns())
            if (!error.isEmpty() && turn.error == error) {
                notice({});
                refresh();
                return;
            }
        notice(error);
    });
    connect(&session_, &ChatSession::threadOpened, this, [this] {
        if (followOpenedThread_) {
            state_.home = false;
            state_.history = false;
        }
        state_.openError.clear();
        notice({});
        render();
        openPendingThreadLink();
    });
    connect(&session_, &ChatSession::threadOpenFailed, this, [this](const QString& message) {
        state_.openError = followOpenedThread_ ? message : QString{};
        render();
    });
    connect(&session_, &ChatSession::catalogChanged, this, [this] {
        state_.models = session_.models();
        normalizeModel();
        render();
    });
    connect(&session_, &ChatSession::modelSelectionChanged, this,
            [this](const QString& model, const QString& effort) {
                if (!model.isEmpty())
                    state_.model = model;
                state_.effort = effort;
                normalizeModel();
                render();
            });
    connect(&session_, &ChatSession::historyReady, this,
            [this](const QJsonArray& threads, const QString& cursor) {
                // App-server can return the same thread more than once, even
                // within one page. Identity is the thread ID, never its title.
                QJsonArray merged;
                QHash<QString, int> positions;
                const auto append = [&](const QJsonArray& values) {
                    for (const auto& value : values) {
                        const auto thread = value.toObject();
                        const auto id = thread.value("id").toString();
                        if (id.isEmpty())
                            continue;
                        const auto found = positions.constFind(id);
                        if (found == positions.cend()) {
                            positions.insert(id, int(merged.size()));
                            merged.append(thread);
                        } else if (thread.value("updatedAt").toInteger() >=
                                   merged[*found].toObject().value("updatedAt").toInteger())
                            merged[*found] = thread;
                    }
                };
                if (state_.historyMore)
                    append(state_.threads);
                append(threads);
                state_.threads = merged;
                state_.historyCursor = cursor;
                state_.historyLoading = false;
                state_.historyMore = false;
                state_.historyLoaded = true;
                state_.historyError.clear();
                render();
            });
    connect(&session_, &ChatSession::historyFailed, this, [this](const QString& message) {
        state_.historyLoading = false;
        state_.historyMore = false;
        state_.historyError = message;
        render();
    });
    connect(&session_, &ChatSession::olderTurnsLoaded, this, [this] {
        olderRequestInFlight_ = false;
        if (!pendingOlderEndId_.isEmpty()) {
            const auto anchor = std::exchange(pendingOlderEndId_, QString{});
            prependCodexHistoryWindow(state_, session_.turns(), anchor);
            render();
        }
    });
    connect(&session_, &ChatSession::loginUrl, this, [this](const QString& url) {
        const QUrl target(url);
        if (target.scheme() == "https")
            context_.env().openExternal(target);
        else
            notice("Codex returned an invalid sign-in URL.");
    });
    connect(&session_, &ChatSession::interaction, this, &CodexViewProvider::interaction);
    connect(&session_.server(), &AppServer::connected, this, [this] {
        notice({});
        refresh();
        openPendingThreadLink();
    });
    connect(&session_.server(), &AppServer::disconnected, this, [this](const QString& reason) {
        resetInteractions();
        notice(reason);
        refresh();
    });
    connect(&session_, &ChatSession::accountChanged, this, [this] {
        if (!session_.accountKnown())
            return;
        const bool wasSignedOut = codexSignedOut(state_);
        const bool firstAccount = !state_.accountKnown;
        if (!session_.account().isEmpty() || !session_.requiresAuth()) {
            apiKeyDraft_.clear();
            state_.apiKeyEntry = false;
            if (wasSignedOut || firstAccount) {
                state_.history = false;
                state_.home = true;
                loadHistory();
            }
            openPendingThreadLink();
        } else if (session_.accountKnown()) {
            state_.confirmingLogout = false;
            state_.renaming = false;
            state_.threads = {};
            state_.historyCursor.clear();
            state_.historyLoading = false;
            state_.historyMore = false;
            state_.historyLoaded = false;
            state_.historyError.clear();
        }
        refresh();
    });
    connect(&session_, &ChatSession::errorCleared, this, [this] { notice({}); });
    connect(&session_, &ChatSession::submissionAccepted, this, [this] {
        editTurn_.clear();
        editItem_.clear();
        savedDraft_.clear();
        state_.editing = false;
        state_.goalMode = false;
        if (state_.draft == submittedDraft_)
            state_.draft.clear();
        for (const auto& path : submittedAttachments_)
            state_.attachments.removeAll(path);
        submittedDraft_.clear();
        submittedAttachments_.clear();
        state_.turnWindowEndId.clear();
        state_.history = false;
        state_.home = false;
        context_.workspaceState().update("draft", state_.draft);
        render();
        if (view_) view_->scrollToEnd("codex.conversation");
    });
    connect(&context_, &ExtensionContext::workspaceChanged, this, [this](const QString& root) {
        followOpenedThread_ = true;
        state_.goalMode = false;
        state_.planMode = context_.workspaceState().get("planMode", false).toBool();
        state_.draft.clear();
        state_.attachments.clear();
        state_.history = false;
        state_.home = true;
        session_.setWorkspace(root);
        if (session_.server().ready())
            loadHistory();
        render();
    });
}
CodexViewProvider::~CodexViewProvider() {
    interactionCancellation_->cancel();
    refreshTimer_.stop();
    historyFilterTimer_.stop();
    for (auto& subscription : subscriptions_)
        subscription.dispose();
    session_.server().stop();
}
ProviderResult<void> CodexViewProvider::resolveView(std::shared_ptr<NativeView> view,
                                                    CancellationToken token) {
    if (resolved_)
        throw std::runtime_error("Codex view is already resolved.");
    resolved_ = true;
    view_ = std::move(view);
    session_.setViewVisible(view_->visible());
    const QPointer<CodexViewProvider> self(this);
    subscriptions_.push_back(view_->onDidReceiveMessage()([self](const NativeViewEvent& event) {
        if (self)
            self->input(event);
    }));
    subscriptions_.push_back(view_->onDidChangeVisibility()([self](const std::monostate&) {
        if (self && self->view_)
            self->session_.setViewVisible(self->view_->visible());
    }));
    subscriptions_.push_back(view_->onDidDispose()([self](const std::monostate&) {
        if (self) {
            self->session_.setViewVisible(false);
            self->session_.interrupt();
            self->view_.reset();
        }
    }));
    subscriptions_.push_back(token.onCancellationRequested([self] {
        if (self)
            self->session_.server().stop();
    }));
    session_.setWorkspace(context_.workspacePath());
    lastOpenThread_ = session_.threadId();
    render();
    QString executable =
        context_.workspace().getConfiguration("codex")->get("executablePath", "").toString();
    if (executable.isEmpty()) {
#ifdef Q_OS_WIN
        executable = context_.asAbsolutePath("runtime/bin/codex.exe");
#else
        executable = context_.asAbsolutePath("runtime/bin/codex");
#endif
    }
    if (!QFileInfo(executable).isExecutable()) {
        const auto located = QStandardPaths::findExecutable(executable);
        if (!located.isEmpty())
            executable = located;
        else {
            notice("Codex executable not found. Import the complete Codex extension folder or "
                   "configure codex.executablePath.");
            return std::monostate{};
        }
    }
    session_.connectRuntime(executable);
    return std::monostate{};
}
void CodexViewProvider::render() {
    if (!view_)
        return;
    updatePresentation();
    auto content = codexViewLayout(state_);
    applyCodexViewIcons(content);
    view_->setContent(content);
    layoutSignature_ = presentationSignature();
}
void CodexViewProvider::normalizeModel() {
    QJsonObject selected;
    for (const auto& value : state_.models) {
        const auto model = value.toObject();
        if (model.value("model").toString(model.value("id").toString()) == state_.model) {
            selected = model;
            break;
        }
    }
    if (selected.isEmpty() && !state_.models.isEmpty()) {
        selected = state_.models.first().toObject();
        for (const auto& value : state_.models)
            if (value.toObject().value("isDefault").toBool()) {
                selected = value.toObject();
                break;
            }
        state_.model = selected.value("model").toString(selected.value("id").toString());
    }
    const auto efforts = selected.value("supportedReasoningEfforts").toArray();
    if (!efforts.isEmpty()) {
        bool supported = false;
        for (const auto& value : efforts)
            supported |= value.toObject().value("reasoningEffort").toString() == state_.effort;
        if (!supported)
            state_.effort =
                selected.value("defaultReasoningEffort")
                    .toString(efforts.first().toObject().value("reasoningEffort").toString());
    }
    if (state_.fastMode && codexFastTier(state_).isEmpty()) {
        state_.fastMode = false;
        context_.globalState().update("fastMode", false);
    }
}
void CodexViewProvider::notice(const QString& text) {
    const bool visibilityChanged = state_.notice.isEmpty() != text.isEmpty();
    state_.notice = text;
    if (visibilityChanged || codexStarting(state_))
        render();
    else if (view_)
        view_->setText("codex.notice", text);
}
void CodexViewProvider::input(const NativeViewEvent& event) {
    if (event.controlId == "codex.renameInput" && state_.renaming) {
        state_.renameDraft = event.text;
        if (view_)
            view_->setEnabled("codex.renameSave", !event.text.trimmed().isEmpty());
        if (event.type == "submitted")
            finishRename(true);
        return;
    }
    if (event.controlId == "codex.renameSave") {
        finishRename(true);
        return;
    }
    if (event.controlId == "codex.renameCancel" ||
        (event.controlId == "codex.renameDialog" && event.type == "dismissed")) {
        finishRename(false);
        return;
    }
    if (handleAuthAction(event) || handleApprovalAction(event) || handleMenuAction(event) || handlePresentationAction(event) || handleComposerAction(event))
        return;
    if (event.controlId == "codex.prompt" && event.type == "filesPasted") {
        if (!session_.busy()) {
            const auto paths = QJsonDocument::fromJson(event.text.toUtf8()).array();
            for (const auto& value : paths) {
                const auto path = value.toString();
                if (QFileInfo(path).isFile() && !state_.attachments.contains(path))
                    state_.attachments.append(path);
            }
            render();
        }
        return;
    }
    if (event.controlId == "codex.prompt") {
        const bool couldSend = codexCanSend(state_);
        state_.draft = event.text;
        context_.workspaceState().update("draft", state_.draft);
        if (view_) {
            view_->setEnabled("codex.send", codexCanSend(state_));
            if (!state_.editing && couldSend != codexCanSend(state_))
                render();
        }
        if (event.type == "submitted")
            submit();
    } else if (event.controlId == "codex.send")
        submit();
    else if (event.controlId == "codex.stop")
        session_.interrupt();
    else if (event.controlId == "codex.new") {
        pendingThreadLink_.clear();
        session_.newChat();
        state_.openError.clear();
        lastOpenThread_.clear();
        state_.history = false;
        state_.home = session_.threadId().isEmpty();
        loadHistory();
        render();
    } else if (event.controlId == "codex.history") {
        if (!state_.history)
            historyReturnToConversation_ = !state_.home;
        state_.history = true;
        state_.historyFilter.clear();
        loadHistory();
        render();
    } else if (event.controlId == "codex.options" && event.type == "selected")
        options(event.text);
    else if (event.controlId == "codex.threads" && event.index >= 0 &&
             event.index < codexFilteredThreads(state_).size() && !session_.busy()) {
        openThread(codexFilteredThreads(state_)[event.index].value("id").toString());
    }
}
void CodexViewProvider::submit(bool goal) {
    goal = goal || state_.goalMode;
    if (goal && state_.draft.trimmed().isEmpty()) {
        notice("Enter a goal objective before sending.");
        return;
    }
    if (session_.busy() || (!state_.home && !state_.openError.isEmpty()) ||
        (state_.draft.trimmed().isEmpty() && state_.attachments.isEmpty()))
        return;
    if (state_.home && !session_.threadId().isEmpty()) {
        session_.newChat();
        if (!session_.threadId().isEmpty())
            return;
    }
    session_.setPlanMode(state_.planMode);
    session_.setApprovalMode(state_.approvalMode);
    session_.setServiceTier(state_.fastMode ? codexFastTier(state_).value("id").toString()
                                            : QString{});
    if (state_.editing) {
        if (goal) {
            notice("Finish or cancel the prompt edit before setting a goal.");
            return;
        }
        submittedDraft_ = state_.draft;
        submittedAttachments_ = state_.attachments;
        session_.editAndSubmit(editTurn_, editItem_, state_.draft, state_.model, state_.effort);
        return;
    }
    QString prompt = state_.draft;
    if (state_.includeEditor)
        if (const auto editor = context_.window().activeTextEditor()) {
            const auto document = editor->document();
            const Range range(editor->selection().start(), editor->selection().end());
            prompt += "\n\nActive file: " + document->fileName() + "\n" +
                      document
                          ->getText(range.isEmpty() ? std::optional<Range>{}
                                                    : std::optional<Range>(range))
                          .left(128 * 1024);
        }
    QJsonArray input;
    for (const auto& path : state_.attachments) {
        if (codexImageFile(path)) {
            const auto saved = attachmentStore_.imagePath(path);
            if (saved.isEmpty()) {
                notice("Could not read image attachment: " + path);
                return;
            }
            input.append(QJsonObject{{"type", "localImage"}, {"path", saved}});
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
                notice("Attachment is unreadable or larger than 1 MiB: " + path);
                return;
            }
            prompt += "\n\nFile: " + path + "\n" + QString::fromUtf8(file.readAll());
        }
    }
    if (!prompt.trimmed().isEmpty())
        input.prepend(QJsonObject{{"type", "text"}, {"text", prompt}});
    submittedDraft_ = state_.draft;
    submittedAttachments_ = state_.attachments;
    session_.submit(input, state_.model, state_.effort, goal);
}
void CodexViewProvider::options(const QString& action) {
    if (session_.threadId().isEmpty() || state_.home || state_.history || session_.opening())
        return;
    if (action == "Copy") {
        QStringList messages;
        for (const auto& turn : session_.turns()) {
            for (const auto& item : turn.items) {
                if (item.value("type") != "userMessage")
                    continue;
                QStringList parts;
                for (const auto& part : item.value("content").toArray()) {
                    const auto text = part.toObject().value("text").toString();
                    if (!text.isEmpty())
                        parts.append(text);
                }
                if (!parts.isEmpty())
                    messages.append("You:\n" + parts.join('\n'));
            }
            const auto response = codexFinalResponse(turn);
            if (!response.isEmpty())
                messages.append("Codex:\n" + response);
        }
        context_.env()
            .clipboard()
            .writeText(messages.join("\n\n"))
            .onFailed(this, [this](const std::exception& error) {
                notice(QString::fromUtf8(error.what()));
            });
        return;
    }
    if (!session_.canWrite() || session_.busy())
        return;
    if (action == "Archive") {
        session_.archive();
    } else if (action == "Rename") {
        renameThread_ = session_.threadId();
        state_.renameDraft = state_.title;
        state_.renaming = true;
        render();
    }
}
void CodexViewProvider::finishRename(bool save) {
    if (!state_.renaming)
        return;
    const auto name = state_.renameDraft.trimmed();
    if (save && name.isEmpty())
        return;
    const bool apply =
        save && session_.threadId() == renameThread_ && session_.canWrite() && !session_.busy();
    state_.renaming = false;
    state_.renameDraft.clear();
    renameThread_.clear();
    render();
    if (apply)
        session_.rename(name);
}
void CodexViewProvider::interaction(const QJsonValue& id, const QString& method,
                                    const QJsonObject& params) {
    if (method == "item/tool/requestUserInput") {
        questions(id, params.value("questions").toArray(), 0, {}, interactionEpoch_);
        return;
    }
    if (method != "item/commandExecution/requestApproval" &&
        method != "item/fileChange/requestApproval" &&
        method != "item/permissions/requestApproval") {
        session_.server().reject(id, "Unsupported Codex interaction: " + method);
        return;
    }
    for (const auto& pending : approvals_)
        if (pending.id == id) return;
    approvals_.append({id, method, "codex.approval." + QString::number(++approvalSerial_),
                       params, codexApprovalPrompt(method, params)});
    if (approvals_.size() == 1)
        showPendingApproval();
}
void CodexViewProvider::resetInteractions() {
    ++interactionEpoch_;
    const auto pending = std::exchange(approvals_, {});
    for (const auto& approval : pending)
        session_.server().respond(approval.id,
            codexApprovalResponse(approval.method, approval.params, std::nullopt));
    state_.approval.reset();
    state_.approvalKey.clear();
    state_.approvalExpanded = false;
    auto previous =
        std::exchange(interactionCancellation_, std::make_shared<CancellationTokenSource>());
    if (previous)
        previous->cancel();
}
void CodexViewProvider::questions(const QJsonValue& id, const QJsonArray& questions, int index,
                                  QJsonObject answers, quint64 epoch) {
    if (epoch != interactionEpoch_) {
        session_.server().reject(id, "Question expired after the conversation changed.");
        return;
    }
    if (index >= questions.size()) {
        session_.server().respond(id, {{"answers", answers}});
        return;
    }
    const auto question = questions[index].toObject();
    InputBoxOptions input;
    input.prompt = question.value("question").toString();
    input.password = question.value("isSecret").toBool();
    const std::function<void(const std::optional<QString>&)> finish =
        [this, id, questions, index, answers = std::move(answers), question,
         epoch](const std::optional<QString>& answer) mutable {
            if (epoch != interactionEpoch_) {
                session_.server().reject(id, "Question expired after the conversation changed.");
                return;
            }
            answers.insert(question.value("id").toString(),
                           QJsonObject{{"answers", answer ? QJsonArray{*answer} : QJsonArray{}}});
            this->questions(id, questions, index + 1, std::move(answers), epoch);
        };
    const auto freeInput = [this, id, input, finish, epoch] {
        if (epoch != interactionEpoch_) {
            session_.server().reject(id, "Question expired after the conversation changed.");
            return;
        }
        context_.window().showInputBox(input, interactionCancellation_->token()).then(this, finish);
    };
    struct Choice : QuickPickItem {
        bool custom{};
        QString answer;
    };
    QVector<Choice> choices;
    if (!question.value("isSecret").toBool())
        for (const auto& value : question.value("options").toArray()) {
            const auto option = value.toObject();
            Choice item;
            item.label = option.value("label").toString();
            item.description = option.value("description").toString();
            item.answer = item.label;
            choices.append(item);
        }
    if (choices.isEmpty()) {
        freeInput();
        return;
    }
    Choice custom;
    custom.label = "Type another answer…";
    custom.custom = true;
    choices.append(custom);
    QuickPickOptions picker;
    picker.title = "Codex has a question";
    picker.placeHolder = question.value("question").toString();
    context_.window()
        .showQuickPick(choices, picker, interactionCancellation_->token())
        .then(this, [finish, freeInput](const std::optional<QuickPickSelection<Choice>>& selected) {
            if (!selected) {
                finish({});
                return;
            }
            const auto* choice = std::get_if<Choice>(&*selected);
            if (!choice) {
                finish({});
                return;
            }
            if (choice->custom)
                freeInput();
            else
                finish(choice->answer);
        });
}
} // namespace litecode::codex
