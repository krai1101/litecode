#include "extensions/ChatHistoryWire.h"
#include "extensions/ChatRequestWire.h"
#include "extensions/ConfigurationWire.h"
#include "extensions/DebugDescriptorWire.h"
#include "extensions/ExtensionApi.h"
#include "extensions/ExtensionMemento.h"
#include "extensions/ExtensionSecretStorage.h"
#include "extensions/ExtensionWireProtocol.h"
#include "extensions/HostChatParticipant.h"
#include "extensions/HostClipboard.h"
#include "extensions/HostConfiguration.h"
#include "extensions/HostDebug.h"
#include "extensions/HostDebugConfigurations.h"
#include "extensions/HostDebugSessions.h"
#include "extensions/HostDebugStarts.h"
#include "extensions/HostDebugTrackers.h"
#include "extensions/HostDocumentRequests.h"
#include "extensions/HostEnvironment.h"
#include "extensions/HostWindow.h"
#include "extensions/HostWorkspace.h"
#include "extensions/IconWire.h"
#include "extensions/JsonMessageTransfer.h"
#include "extensions/MarkdownWire.h"
#include "extensions/WindowStateWire.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMutex>
#include <QMutexLocker>
#include <QPluginLoader>
#include <QPointer>
#include <QPromise>
#include <QThread>
#include <QTimer>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace litecode::extensions;
namespace {
QByteArray hashFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result().toHex();
}

void sendFrame(QLocalSocket& socket, const QJsonObject& message) {
    if (socket.state() != QLocalSocket::ConnectedState)
        return;
    const auto frame = wire::encode(message);
    if (frame.isEmpty()) {
        qApp->quit();
        return;
    }
    socket.write(frame);
}

QJsonValue encodeResource(const ChatResource& resource) {
    return std::visit(
        [](const auto& value) -> QJsonValue {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QUrl>) {
                return value.toString();
            } else {
                const auto position = [](const Position& point) {
                    return QJsonObject{{"line", point.line()}, {"character", point.character()}};
                };
                return QJsonObject{{"uri", value.uri.toString()},
                                   {"range", QJsonObject{{"start", position(value.range.start())},
                                                         {"end", position(value.range.end())}}}};
            }
        },
        resource);
}

QJsonValue encodeIconPath(const IconPath& iconPath) { return wire::encodeIconPath(iconPath); }

QJsonObject encodeFileTree(const ChatResponseFileTree& node) {
    QJsonObject encoded{{"name", node.name}};
    if (node.children) {
        QJsonArray children;
        for (const auto& child : *node.children)
            children.append(encodeFileTree(child));
        encoded.insert("children", children);
    }
    return encoded;
}

QJsonObject encodeChatPart(const ChatResponsePart& part) {
    return std::visit(
        [](const auto& value) -> QJsonObject {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ChatResponseMarkdownPart>) {
                if (const auto trusted = std::get_if<bool>(&value.value.isTrusted);
                    trusted && *trusted)
                    throw std::invalid_argument("The boolean true form of MarkdownString.isTrusted "
                                                "is not supported for chat participants.");
                const auto markdown = wire::encodeMarkdownString(value.value);
                return {{"kind", "markdown"}, {"value", markdown}};
            } else if constexpr (std::is_same_v<T, ChatResponseFileTreePart>) {
                QJsonArray nodes;
                for (const auto& node : value.value)
                    nodes.append(encodeFileTree(node));
                return {
                    {"kind", "filetree"}, {"value", nodes}, {"baseUri", value.baseUri.toString()}};
            } else if constexpr (std::is_same_v<T, ChatResponseAnchorPart>) {
                QJsonObject encoded{{"kind", "anchor"}, {"value", encodeResource(value.value)}};
                if (value.title)
                    encoded.insert("title", *value.title);
                return encoded;
            } else if constexpr (std::is_same_v<T, ChatResponseProgressPart>) {
                return {{"kind", "progress"}, {"value", value.value}};
            } else if constexpr (std::is_same_v<T, ChatResponseReferencePart>) {
                QJsonObject encoded{{"kind", "reference"}, {"value", encodeResource(value.value)}};
                if (value.iconPath)
                    encoded.insert("iconPath", encodeIconPath(*value.iconPath));
                return encoded;
            } else {
                QJsonObject command{{"title", value.value.title}, {"command", value.value.command}};
                if (value.value.tooltip)
                    command.insert("tooltip", *value.value.tooltip);
                if (!value.value.arguments.isEmpty())
                    command.insert("arguments", value.value.arguments);
                return {{"kind", "button"}, {"value", command}};
            }
        },
        part);
}

class HostChatResponseStream final : public ChatResponseStream,
                                     public std::enable_shared_from_this<HostChatResponseStream> {
  public:
    HostChatResponseStream(QObject* dispatcher, std::function<void(const QJsonObject&)> send)
        : dispatcher_(dispatcher), send_(std::move(send)) {}
    void push(const ChatResponsePart& part) override {
        const auto encoded = encodeChatPart(part);
        {
            QMutexLocker lock(&mutex_);
            if (!active_)
                throw CancellationError();
            pending_.append(encoded);
        }
        if (dispatcher_)
            QMetaObject::invokeMethod(
                dispatcher_, [self = shared_from_this()] { self->flush(); }, Qt::QueuedConnection);
    }
    void flush() {
        QJsonArray ready;
        {
            QMutexLocker lock(&mutex_);
            ready = std::exchange(pending_, {});
        }
        for (const auto& value : ready)
            send_(value.toObject());
    }
    void finish() {
        {
            QMutexLocker lock(&mutex_);
            active_ = false;
        }
        flush();
    }
    void cancel() {
        QMutexLocker lock(&mutex_);
        active_ = false;
        pending_ = {};
    }

  private:
    QPointer<QObject> dispatcher_;
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QJsonArray pending_;
    bool active_{true};
};

class HostContext final : public ExtensionContext, public Commands, public Chat {
  public:
    HostContext(QLocalSocket& socket, QString id, QString workspace, QString libraryPath,
                QString storageRoot, ConfigurationModel configuration, EnvironmentInfo environment,
                WindowState windowState, const QVector<TextDocumentSnapshot>& initialDocuments,
                const WindowEditorState& editorState)
        : socket_(socket), id_(std::move(id)), workspace_(std::move(workspace)),
          extensionPath_(QFileInfo(libraryPath).absolutePath()),
          storageRoot_(QDir(storageRoot).absolutePath()) {
        for (const auto& parent : {"globalStorage", "logs"})
            if (!QDir().mkpath(QDir(storageRoot_).filePath(parent)))
                throw std::runtime_error("Could not create extension storage directory.");
        globalState_.setFilePath(QDir(storageRoot_).filePath("mementos/global/" + id_ + ".json"));
        secrets_ = std::make_unique<ExtensionSecretStorage>(
            QDir(storageRoot_).filePath("secrets/" + id_ + ".json"), id_);
        configureWorkspaceStorage();
        configuration_ = std::make_unique<HostConfiguration>(
            [this](const QJsonObject& message) { send(message); }, std::move(configuration));
        documentRequests_ = std::make_unique<HostDocumentRequests>(
            [this](const QJsonObject& message) { send(message); });
        workspaceRequests_ = std::make_unique<HostWorkspaceRequests>(
            [this](const QJsonObject& message) { send(message); });
        documents_ = std::make_unique<HostDocuments>(
            [this](const QString& id) { return documentRequests_->save(id); });
        for (const auto& document : initialDocuments)
            documents_->accept(document, true);
        contentProviders_ = std::make_unique<HostContentProviders>(
            *documents_, [this](const QJsonObject& message) { send(message); });
        workspaceApi_ = std::make_unique<HostWorkspace>(*configuration_, *documents_,
                                                        *workspaceRequests_, *contentProviders_);
        window_ = std::make_unique<HostWindow>(
            logUri().toLocalFile(), [this](const QJsonObject& message) { send(message); }, id_,
            windowState);
        window_->attachEditors(*documents_, *workspaceRequests_);
        if (!window_->editors().accept(editorState, true))
            throw std::runtime_error("Invalid initial editor context.");
        window_->output().setGlobalLogLevel(environment.logLevel);
        environment_ = std::make_unique<HostEnvironment>(
            std::move(environment), [this](const QJsonObject& message) { send(message); });
        debug_ = std::make_unique<HostDebug>([this](const QJsonObject& message) { send(message); });
    }

    QString extensionId() const override { return id_; }
    Memento& workspaceState() override { return workspaceState_; }
    GlobalMemento& globalState() override { return globalState_; }
    SecretStorage& secrets() override { return *secrets_; }
    std::optional<QUrl> storageUri() const override {
        return workspaceStorage_.isEmpty()
                   ? std::nullopt
                   : std::optional<QUrl>(QUrl::fromLocalFile(workspaceStorage_));
    }
    QUrl globalStorageUri() const override {
        return QUrl::fromLocalFile(QDir(storageRoot_).filePath("globalStorage/" + id_));
    }
    QUrl logUri() const override {
        return QUrl::fromLocalFile(QDir(storageRoot_).filePath("logs/" + id_));
    }
    QUrl extensionUri() const override { return QUrl::fromLocalFile(extensionPath_); }
    QString extensionPath() const override { return extensionPath_; }
    QString asAbsolutePath(const QString& relativePath) const override {
        return QDir::cleanPath(QDir(extensionPath_).absoluteFilePath(relativePath));
    }
    QString workspacePath() const override { return workspace_; }
    Commands& commands() override { return *this; }
    Workspace& workspace() override { return *workspaceApi_; }
    Window& window() override { return *window_; }
    Environment& env() override { return *environment_; }
    Debug& debug() override { return *debug_; }
    HostDebug& hostDebug() { return *debug_; }
    HostClipboard& hostClipboard() { return environment_->hostClipboard(); }
    HostEnvironment& hostEnvironment() { return *environment_; }
    HostOutput& output() { return window_->output(); }
    HostMessages& messages() { return window_->messages(); }
    HostDialogs& dialogs() { return window_->dialogs(); }
    HostInputBox& inputBox() { return window_->inputBox(); }
    HostQuickPick& quickPick() { return window_->quickPick(); }
    HostUriHandlers& uriHandlers() { return window_->uriHandlers(); }
    HostNativeViews& nativeViews() { return window_->nativeViews(); }
    HostWindowState& windowState() { return window_->windowState(); }
    HostEditors& editors() { return window_->editors(); }
    HostProgress& progress() { return window_->progress(); }
    HostConfiguration& hostConfiguration() { return *configuration_; }
    HostDocuments& hostDocuments() { return *documents_; }
    HostContentProviders& contentProviders() { return *contentProviders_; }
    HostDocumentRequests& documentRequests() { return *documentRequests_; }
    HostWorkspaceRequests& workspaceRequests() { return *workspaceRequests_; }
    Chat& chat() override { return *this; }
    void setWorkspace(const QString& path) {
        if (workspace_ == path)
            return;
        workspace_ = path;
        try {
            configureWorkspaceStorage();
        } catch (const std::exception& error) {
            send({{"event", "failed"}, {"message", QString::fromUtf8(error.what())}});
            qApp->quit();
            return;
        }
        emit workspaceChanged(path);
    }
    Disposable registerCommand(const QString& id, CommandHandler callback) override {
        if (!accepting_)
            throw std::runtime_error("Extension is shutting down.");
        if (id.trimmed().isEmpty() || id != id.trimmed() || !callback)
            throw std::invalid_argument("Invalid extension command registration.");
        if (commands_.contains(id))
            throw std::invalid_argument("Command is already registered.");
        const auto token = ++nextToken_;
        commands_.insert(id, {std::move(callback), token});
        send({{"event", "registeredCommand"}, {"id", id}});
        return Disposable([self = QPointer<HostContext>(this), id, token] {
            if (!self)
                return;
            const auto it = self->commands_.constFind(id);
            if (it == self->commands_.cend() || it->token != token)
                return;
            self->commands_.remove(id);
            if (self->accepting_)
                self->send({{"event", "unregisteredCommand"}, {"id", id}});
        });
    }
    QFuture<QJsonValue> executeCommand(const QString& id,
                                       const QJsonArray& arguments = {}) override {
        auto promise = std::make_shared<QPromise<QJsonValue>>();
        promise->start();
        auto future = promise->future();
        if (!accepting_ || id.trimmed().isEmpty()) {
            failPromise(promise, "Invalid or unavailable command.");
            return future;
        }
        const auto requestId = ++nextClientRequestId_;
        pendingCommandCalls_.insert(requestId, promise);
        send({{"event", "executeCommandRequest"},
              {"requestId", QString::number(requestId)},
              {"id", id},
              {"arguments", arguments}});
        return future;
    }
    QFuture<QStringList> getCommands(bool filterInternal = false) override {
        auto promise = std::make_shared<QPromise<QStringList>>();
        promise->start();
        auto future = promise->future();
        if (!accepting_) {
            failPromise(promise, "Extension is shutting down.");
            return future;
        }
        const auto requestId = ++nextClientRequestId_;
        pendingCommandLists_.insert(requestId, promise);
        send({{"event", "getCommandsRequest"},
              {"requestId", QString::number(requestId)},
              {"filterInternal", filterInternal}});
        return future;
    }
    void completeCommandCall(quint64 requestId, const QJsonValue& value, const QString& error) {
        auto promise = pendingCommandCalls_.take(requestId);
        if (!promise)
            return;
        if (!error.isEmpty())
            failPromise(promise, error);
        else {
            promise->addResult(value);
            promise->finish();
        }
    }
    void completeCommandList(quint64 requestId, const QJsonArray& values, const QString& error) {
        auto promise = pendingCommandLists_.take(requestId);
        if (!promise)
            return;
        if (!error.isEmpty()) {
            failPromise(promise, error);
            return;
        }
        QStringList result;
        for (const auto& value : values) {
            if (!value.isString()) {
                failPromise(promise, "Invalid command list from workbench.");
                return;
            }
            result.append(value.toString());
        }
        promise->addResult(result);
        promise->finish();
    }
    std::shared_ptr<ChatParticipant> createChatParticipant(const QString& id,
                                                           ChatRequestHandler handler) override {
        if (!accepting_ || id.trimmed().isEmpty() || id != id.trimmed() || !handler ||
            chatParticipants_.contains(id))
            throw std::invalid_argument("Invalid or duplicate chat participant registration.");
        const auto token = ++nextToken_;
        auto participant = std::make_shared<HostChatParticipant>(
            *this, id, std::move(handler),
            [self = QPointer<HostContext>(this), id, token] {
                if (!self)
                    return;
                const auto it = self->chatParticipants_.constFind(id);
                if (it == self->chatParticipants_.cend() || it->token != token)
                    return;
                self->chatParticipants_.remove(id);
                if (self->accepting_)
                    self->send({{"event", "unregisteredChatParticipant"}, {"id", id}});
            },
            [self = QPointer<HostContext>(this), id, token](const QJsonValue& value,
                                                            bool hasFollowups) {
                if (!self || !self->accepting_)
                    return;
                const auto current = self->chatParticipants_.constFind(id);
                if (current == self->chatParticipants_.cend() || current->token != token)
                    return;
                QJsonObject message{{"event", "chatParticipantIconChanged"}, {"id", id}};
                message.insert("hasFollowups", hasFollowups);
                if (!value.isUndefined())
                    message.insert("iconPath", value);
                self->send(message);
            });
        chatParticipants_.insert(id, {token, participant});
        send({{"event", "registeredChatParticipant"}, {"id", id}});
        return participant;
    }
    void receiveChatFeedback(const QString& participantId, const QJsonObject& rawResult,
                             int rawKind) {
        const auto it = chatParticipants_.constFind(participantId);
        if (it == chatParticipants_.cend() || (rawKind != 0 && rawKind != 1))
            return;
        ChatResult result;
        result.metadata = rawResult.value("metadata").toObject();
        if (rawResult.value("errorDetails").isObject()) {
            const auto error = rawResult.value("errorDetails").toObject();
            result.errorDetails = ChatErrorDetails{error.value("message").toString(),
                                                   error.value("responseIsFiltered").toBool()};
        }
        it->participant->fireFeedback({result, static_cast<ChatResultFeedbackKind>(rawKind)});
    }
    void executeChat(quint64 requestId, const QString& participantId, const QJsonObject& rawRequest,
                     const QJsonArray& history) {
        const auto it = chatParticipants_.constFind(participantId);
        if (it == chatParticipants_.cend() || activeChats_.contains(requestId)) {
            send({{"event", "chatResult"},
                  {"requestId", QString::number(requestId)},
                  {"error", "Chat participant is not registered."}});
            return;
        }
        const auto request = wire::decodeChatRequest(rawRequest);
        if (!request) {
            send({{"event", "chatResult"},
                  {"requestId", QString::number(requestId)},
                  {"error", "Invalid chat request."}});
            return;
        }
        const auto context = wire::decodeChatContext(history, participantId);
        if (!context) {
            send({{"event", "chatResult"},
                  {"requestId", QString::number(requestId)},
                  {"error", "Invalid chat history."}});
            return;
        }
        auto stream = std::make_shared<HostChatResponseStream>(
            this, [self = QPointer<HostContext>(this), requestId](const QJsonObject& part) {
                if (self && self->accepting_)
                    self->send({{"event", "chatPart"},
                                {"requestId", QString::number(requestId)},
                                {"part", part}});
            });
        auto cancellation = std::make_shared<CancellationTokenSource>();
        activeChats_.insert(requestId, {stream, cancellation, participantId, *context});
        try {
            const auto handler = it->participant->requestHandler();
            auto result = handler(*request, *context, stream, cancellation->token());
            if (std::holds_alternative<std::monostate>(result)) {
                finishChat(requestId, {}, {});
                return;
            }
            if (std::holds_alternative<ChatResult>(result)) {
                finishChat(requestId, std::get<ChatResult>(result), {});
                return;
            }
            if (std::holds_alternative<std::optional<ChatResult>>(result)) {
                finishChat(requestId,
                           std::get<std::optional<ChatResult>>(result).value_or(ChatResult{}), {});
                return;
            }
            if (const auto* voidFuture = std::get_if<QFuture<void>>(&result))
                observeChatResult(requestId, *voidFuture);
            else if (const auto* resultFuture = std::get_if<QFuture<ChatResult>>(&result))
                observeChatResult(requestId, *resultFuture);
            else
                observeChatResult(requestId, std::get<QFuture<std::optional<ChatResult>>>(result));
        } catch (const std::exception& error) {
            finishChat(requestId, {}, QString::fromUtf8(error.what()));
        } catch (...) {
            finishChat(requestId, {}, "Chat participant failed.");
        }
    }
    template <typename Value> void observeChatResult(quint64 requestId, QFuture<Value> future) {
        auto* watcher = new QFutureWatcher<Value>(this);
        connect(watcher, &QFutureWatcher<Value>::finished, this, [this, watcher, requestId] {
            try {
                if constexpr (std::is_void_v<Value>) {
                    watcher->future().waitForFinished();
                    finishChat(requestId, {}, {});
                } else if constexpr (std::is_same_v<Value, std::optional<ChatResult>>) {
                    finishChat(requestId, watcher->result().value_or(ChatResult{}), {});
                } else
                    finishChat(requestId, watcher->result(), {});
            } catch (const std::exception& error) {
                finishChat(requestId, {}, QString::fromUtf8(error.what()));
            } catch (...) {
                finishChat(requestId, {}, "Chat participant failed.");
            }
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }
    void cancelChat(quint64 requestId) {
        const auto it = activeChats_.find(requestId);
        if (it == activeChats_.end())
            return;
        it->stream->cancel();
        it->cancellation->cancel();
    }
    void finishChat(quint64 requestId, const ChatResult& result, const QString& error) {
        auto it = activeChats_.find(requestId);
        if (it == activeChats_.end())
            return;
        const auto execution = it.value();
        activeChats_.erase(it);
        if (execution.cancellation->token().isCancellationRequested())
            return;
        execution.stream->finish();
        QJsonObject serialized;
        if (result.errorDetails)
            serialized.insert(
                "errorDetails",
                QJsonObject{{"message", result.errorDetails->message},
                            {"responseIsFiltered", result.errorDetails->responseIsFiltered}});
        if (!result.metadata.isEmpty())
            serialized.insert("metadata", result.metadata);
        send({{"event", "chatResult"},
              {"requestId", QString::number(requestId)},
              {"result", serialized},
              {"error", error}});
        if (!error.isEmpty())
            return;
        const auto participant = chatParticipants_.value(execution.participantId).participant;
        const auto provider =
            participant ? participant->followupProvider() : std::shared_ptr<ChatFollowupProvider>{};
        if (!provider) {
            send({{"event", "chatFollowups"},
                  {"requestId", QString::number(requestId)},
                  {"followups", QJsonArray{}},
                  {"error", ""}});
            return;
        }
        auto cancellation = std::make_shared<CancellationTokenSource>();
        activeFollowups_.insert(requestId, cancellation);
        try {
            auto followups =
                provider->provideFollowups(result, execution.context, cancellation->token());
            std::visit(
                [this, requestId](auto pending) {
                    using T = std::decay_t<decltype(pending)>;
                    using Items = QVector<ChatFollowup>;
                    if constexpr (std::is_same_v<T, Items>)
                        completeFollowups(requestId, pending, {});
                    else if constexpr (std::is_same_v<T, std::optional<Items>>)
                        completeFollowups(requestId, pending.value_or(Items{}), {});
                    else {
                        using Value = std::conditional_t<std::is_same_v<T, QFuture<Items>>, Items,
                                                         std::optional<Items>>;
                        auto* watcher = new QFutureWatcher<Value>(this);
                        connect(watcher, &QFutureWatcher<Value>::finished, this,
                                [this, watcher, requestId] {
                                    try {
                                        if constexpr (std::is_same_v<Value, Items>)
                                            completeFollowups(requestId, watcher->result(), {});
                                        else
                                            completeFollowups(
                                                requestId, watcher->result().value_or(Items{}), {});
                                    } catch (const std::exception& failure) {
                                        completeFollowups(requestId, {},
                                                          QString::fromUtf8(failure.what()));
                                    } catch (...) {
                                        completeFollowups(requestId, {},
                                                          "Followup provider failed.");
                                    }
                                    watcher->deleteLater();
                                });
                        watcher->setFuture(pending);
                    }
                },
                std::move(followups));
        } catch (const std::exception& failure) {
            completeFollowups(requestId, {}, QString::fromUtf8(failure.what()));
        } catch (...) {
            completeFollowups(requestId, {}, "Followup provider failed.");
        }
    }
    void completeFollowups(quint64 requestId, const QVector<ChatFollowup>& followups,
                           const QString& error) {
        auto cancellation = activeFollowups_.take(requestId);
        if (!cancellation || cancellation->token().isCancellationRequested())
            return;
        QJsonArray values;
        if (error.isEmpty()) {
            for (const auto& followup : followups) {
                if (followup.prompt.trimmed().isEmpty()) {
                    send({{"event", "chatFollowups"},
                          {"requestId", QString::number(requestId)},
                          {"followups", QJsonArray{}},
                          {"error", "Followup prompt is empty."}});
                    return;
                }
                QJsonObject value{{"prompt", followup.prompt}};
                if (followup.label)
                    value.insert("label", *followup.label);
                if (followup.participant)
                    value.insert("participant", *followup.participant);
                if (followup.command)
                    value.insert("command", *followup.command);
                values.append(value);
            }
        }
        send({{"event", "chatFollowups"},
              {"requestId", QString::number(requestId)},
              {"followups", values},
              {"error", error}});
    }
    void cancelFollowups(quint64 requestId) {
        if (auto cancellation = activeFollowups_.take(requestId))
            cancellation->cancel();
    }
    Disposable
    registerDocumentFormatter(const QString& languageId,
                              std::function<QString(const QString&)> formatter) override {
        if (!accepting_ || languageId.trimmed().isEmpty() || !formatter ||
            formatters_.contains(languageId))
            return {};
        const auto token = ++nextToken_;
        formatters_.insert(languageId, {std::move(formatter), token});
        send({{"event", "registeredFormatter"}, {"language", languageId}});
        return Disposable([self = QPointer<HostContext>(this), languageId, token] {
            if (!self)
                return;
            const auto it = self->formatters_.constFind(languageId);
            if (it == self->formatters_.cend() || it->token != token)
                return;
            self->formatters_.remove(languageId);
            if (self->accepting_)
                self->send({{"event", "unregisteredFormatter"}, {"language", languageId}});
        });
    }
    void format(quint64 requestId, const QString& languageId, const QString& source) {
        const auto it = formatters_.constFind(languageId);
        if (it == formatters_.cend()) {
            send({{"event", "formatResult"},
                  {"requestId", QString::number(requestId)},
                  {"error", "Formatter is not registered."}});
            return;
        }
        try {
            const auto callback = it->callback;
            send({{"event", "formatResult"},
                  {"requestId", QString::number(requestId)},
                  {"text", callback(source)}});
        } catch (...) {
            send({{"event", "formatResult"},
                  {"requestId", QString::number(requestId)},
                  {"error", "Formatter failed."}});
        }
    }
    void openFile(const QString& path) override { send({{"event", "openFile"}, {"path", path}}); }
    void showInformationMessage(const QString& message) override {
        send({{"event", "information"}, {"message", message}});
    }
    void execute(quint64 requestId, const QString& id, const QJsonArray& arguments) {
        const auto reply = [this, requestId, id](const QJsonValue& value, const QString& error) {
            QJsonObject message{{"event", "commandResult"},
                                {"id", id},
                                {"requestId", QString::number(requestId)},
                                {"ok", error.isEmpty()},
                                {"error", error}};
            if (error.isEmpty() && !value.isUndefined())
                message.insert("result", value);
            send(message);
        };
        const auto it = commands_.constFind(id);
        if (it == commands_.cend()) {
            reply({}, "Command is not registered: " + id);
            return;
        }
        try {
            const auto callback = it->callback;
            auto result = callback(arguments);
            if (std::holds_alternative<QJsonValue>(result)) {
                reply(std::get<QJsonValue>(result), {});
                return;
            }
            auto* watcher = new QFutureWatcher<QJsonValue>(this);
            connect(watcher, &QFutureWatcher<QJsonValue>::finished, this, [watcher, reply] {
                try {
                    reply(watcher->result(), {});
                } catch (const std::exception& error) {
                    reply({}, QString::fromUtf8(error.what()));
                } catch (...) {
                    reply({}, "Extension command failed.");
                }
                watcher->deleteLater();
            });
            watcher->setFuture(std::get<QFuture<QJsonValue>>(result));
        } catch (const std::exception& error) {
            reply({}, QString::fromUtf8(error.what()));
        } catch (...) {
            reply({}, "Extension command failed: " + id);
        }
    }
    void shutdown() {
        if (shuttingDown_)
            return;
        shuttingDown_ = true;
        accepting_ = false;
        // Both cancellation and promise rejection can synchronously execute
        // extension code. Detach before shutting down any capability service.
        auto chats = std::exchange(activeChats_, {});
        auto followups = std::exchange(activeFollowups_, {});
        auto commandCalls = std::exchange(pendingCommandCalls_, {});
        auto commandLists = std::exchange(pendingCommandLists_, {});
        if (workspaceApi_)
            workspaceApi_->shutdown();
        if (configuration_)
            configuration_->shutdown();
        if (documents_)
            documents_->shutdown();
        if (documentRequests_)
            documentRequests_->shutdown();
        if (workspaceRequests_)
            workspaceRequests_->shutdown();
        if (window_)
            window_->shutdown();
        if (environment_)
            environment_->shutdown();
        if (debug_)
            debug_->shutdown();
        for (auto& execution : chats) {
            execution.stream->cancel();
            execution.cancellation->cancel();
        }
        for (auto& cancellation : followups)
            cancellation->cancel();
        chatParticipants_.clear();
        for (auto& promise : commandCalls)
            failPromise(promise, "Extension host stopped before command completed.");
        for (auto& promise : commandLists)
            failPromise(promise, "Extension host stopped before command list completed.");
        disposeSubscriptions();
        commands_.clear();
        formatters_.clear();
    }

  private:
    void configureWorkspaceStorage() {
        if (workspace_.isEmpty()) {
            workspaceStorage_.clear();
            workspaceState_.setFilePath({});
            return;
        }
        auto identity = QFileInfo(workspace_).canonicalFilePath();
        if (identity.isEmpty())
            identity = QFileInfo(workspace_).absoluteFilePath();
        identity = QDir::cleanPath(identity);
#ifdef Q_OS_WIN
        identity = identity.toCaseFolded();
#endif
        const auto key = QString::fromLatin1(
            QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
        const auto parent = QDir(storageRoot_).filePath("workspaceStorage/" + key);
        if (!QDir().mkpath(parent))
            throw std::runtime_error("Could not create workspace storage directory.");
        workspaceStorage_ = QDir(parent).filePath(id_);
        workspaceState_.setFilePath(
            QDir(storageRoot_).filePath("mementos/workspace/" + key + '/' + id_ + ".json"));
    }
    template <typename T>
    static void failPromise(const std::shared_ptr<QPromise<T>>& promise, const QString& message) {
        promise->setException(std::make_exception_ptr(std::runtime_error(message.toStdString())));
        promise->finish();
    }
    struct Command {
        CommandHandler callback;
        quint64 token{};
    };
    struct Formatter {
        std::function<QString(const QString&)> callback;
        quint64 token{};
    };
    struct RegisteredChatParticipant {
        quint64 token{};
        std::shared_ptr<HostChatParticipant> participant;
    };
    struct ActiveChat {
        std::shared_ptr<HostChatResponseStream> stream;
        std::shared_ptr<CancellationTokenSource> cancellation;
        QString participantId;
        ChatContext context;
    };
    void send(const QJsonObject& message) { sendFrame(socket_, message); }
    QLocalSocket& socket_;
    QString id_, workspace_, extensionPath_, storageRoot_, workspaceStorage_;
    ExtensionMemento workspaceState_, globalState_;
    std::unique_ptr<ExtensionSecretStorage> secrets_;
    std::unique_ptr<HostConfiguration> configuration_;
    std::unique_ptr<HostWorkspace> workspaceApi_;
    std::unique_ptr<HostDocumentRequests> documentRequests_;
    std::unique_ptr<HostWorkspaceRequests> workspaceRequests_;
    std::unique_ptr<HostWindow> window_;
    std::unique_ptr<HostEnvironment> environment_;
    std::unique_ptr<HostDebug> debug_;
    std::unique_ptr<HostDocuments> documents_;
    std::unique_ptr<HostContentProviders> contentProviders_;
    QHash<QString, Command> commands_;
    QHash<QString, Formatter> formatters_;
    QHash<QString, RegisteredChatParticipant> chatParticipants_;
    QHash<quint64, ActiveChat> activeChats_;
    QHash<quint64, std::shared_ptr<CancellationTokenSource>> activeFollowups_;
    quint64 nextToken_{};
    quint64 nextClientRequestId_{};
    QHash<quint64, std::shared_ptr<QPromise<QJsonValue>>> pendingCommandCalls_;
    QHash<quint64, std::shared_ptr<QPromise<QStringList>>> pendingCommandLists_;
    bool accepting_{true};
    bool shuttingDown_{};
};

class ExtensionHost final : public QObject {
  public:
    ExtensionHost(QString socketName, QString token, QString libraryPath, QString expectedHash,
                  QString extensionId, QString workspace, QString storageRoot)
        : socketName_(std::move(socketName)), token_(std::move(token)),
          libraryPath_(std::move(libraryPath)), expectedHash_(std::move(expectedHash)),
          extensionId_(std::move(extensionId)), workspace_(std::move(workspace)),
          storageRoot_(std::move(storageRoot)) {
        connect(&socket_, &QLocalSocket::connected, this, [this] { connected(); });
        connect(&socket_, &QLocalSocket::readyRead, this, [this] { readMessages(); });
        connect(&socket_, &QLocalSocket::disconnected, qApp, &QCoreApplication::quit);
    }
    void start() { socket_.connectToServer(socketName_); }

  private:
    void send(const QJsonObject& message) { sendFrame(socket_, message); }
    void connected() { send({{"event", "hello"}, {"token", token_}, {"id", extensionId_}}); }
    void activate(ConfigurationModel configuration, EnvironmentInfo environment,
                  WindowState windowState, WindowEditorState editorState) {
        if (loader_ || context_ || active_ || stopping_) {
            qApp->quit();
            return;
        }
        if (hashFile(libraryPath_) != expectedHash_.toLatin1()) {
            send({{"event", "failed"}, {"message", "Extension binary changed before load."}});
            socket_.flush();
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            return;
        }
        loader_ = std::make_unique<QPluginLoader>(libraryPath_);
        plugin_ = qobject_cast<Extension*>(loader_->instance());
        if (!plugin_) {
            send({{"event", "failed"}, {"message", loader_->errorString()}});
            socket_.flush();
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            return;
        }
        try {
            context_ = std::make_unique<HostContext>(
                socket_, extensionId_, workspace_, libraryPath_, storageRoot_,
                std::move(configuration), std::move(environment), windowState,
                initialDocuments_.values(), editorState);
            activating_ = true;
            plugin_->activate(*context_);
            activating_ = false;
        } catch (...) {
            activating_ = false;
            if (context_)
                context_->shutdown();
            // A failed activation is not an activated extension. Upstream does
            // not invoke its deactivate hook; partial registrations still need
            // native cleanup before this process exits.
            send({{"event", "failed"}, {"message", "Extension activation failed."}});
            socket_.flush();
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            return;
        }
        active_ = true;
        if (stopRequested_) {
            stop();
            return;
        }
        send({{"event", "ready"}});
    }
    void readMessages() {
        if (stopping_ || stopRequested_)
            return;
        while (socket_.bytesAvailable() > 0) {
            const auto result = decoder_.consume(socket_.read(64 * 1024));
            if (!result.error.isEmpty()) {
                qApp->quit();
                return;
            }
            for (const auto& envelope : result.messages) {
                if (stopping_ || stopRequested_)
                    return;
                auto message = envelope;
                auto command = message.value("command").toString();
                if (trackerTransferDecoder_.recognizes(message) ||
                    trackerTransferDecoder_.transferring()) {
                    if (!context_ || documentDecoder_.transferring() ||
                        documentChangeDecoder_.transferring() ||
                        documentBatchDecoder_.transferring() ||
                        clipboardTransferDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    QString error;
                    const auto complete = trackerTransferDecoder_.accept(message, &error);
                    if (!error.isEmpty()) {
                        qApp->quit();
                        return;
                    }
                    if (!complete)
                        continue;
                    message = *complete;
                    command = message.value("command").toString();
                }
                if (documentBatchDecoder_.transferring() && command != "documentBatchEnd" &&
                    command != "documentChange" && command != "documentChangeBegin" &&
                    command != "documentChangeChunk" && command != "documentChangeEnd") {
                    qApp->quit();
                    return;
                }
                if (clipboardTransferDecoder_.recognizes(message) ||
                    clipboardTransferDecoder_.transferring()) {
                    if (!context_ || documentDecoder_.transferring() ||
                        documentChangeDecoder_.transferring() ||
                        documentBatchDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    QString error;
                    const auto complete = clipboardTransferDecoder_.accept(message, &error);
                    if (!error.isEmpty()) {
                        qApp->quit();
                        return;
                    }
                    if (!complete)
                        continue;
                    message = *complete;
                    command = message.value("command").toString();
                }
                if (command == "documentBegin" || command == "documentChunk" ||
                    command == "documentEnd") {
                    if (documentChangeDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    QString error;
                    auto document = documentDecoder_.accept(message, &error);
                    if (!error.isEmpty()) {
                        qApp->quit();
                        return;
                    }
                    if (document) {
                        if (context_) {
                            try {
                                context_->hostDocuments().accept(std::move(*document));
                            } catch (...) {
                                qApp->quit();
                                return;
                            }
                        } else {
                            const auto id = document->id;
                            initialDocuments_.insert(id, std::move(*document));
                        }
                    }
                } else if (command == "documentBatchBegin" || command == "documentBatchEnd" ||
                           documentBatchDecoder_.transferring()) {
                    if (!context_ || documentDecoder_.transferring() ||
                        documentChangeDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    QString error;
                    const auto batch = documentBatchDecoder_.accept(message, &error);
                    if (!error.isEmpty()) {
                        qApp->quit();
                        return;
                    }
                    if (batch) {
                        try {
                            context_->hostDocuments().changeBatch(*batch);
                        } catch (...) {
                            qApp->quit();
                            return;
                        }
                    }
                } else if (command == "documentChange" || command == "documentChangeBegin" ||
                           command == "documentChangeChunk" || command == "documentChangeEnd") {
                    if (!context_ || documentDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    QString error;
                    const auto change = documentChangeDecoder_.accept(message, &error);
                    if (!error.isEmpty()) {
                        qApp->quit();
                        return;
                    }
                    if (change) {
                        try {
                            context_->hostDocuments().change(
                                change->id, change->previousVersion, change->version, change->range,
                                change->text, change->dirty, change->reason);
                        } catch (...) {
                            qApp->quit();
                            return;
                        }
                    }
                } else if (command == "documentClose") {
                    if (documentDecoder_.transferring() || documentChangeDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    if (context_)
                        context_->hostDocuments().close(message.value("id").toString());
                    else
                        initialDocuments_.remove(message.value("id").toString());
                } else if (command == "documentMetadata") {
                    const auto metadata = wire::decodeDocumentMetadata(message);
                    if (!context_ || !metadata || documentDecoder_.transferring() ||
                        documentChangeDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    try {
                        context_->hostDocuments().metadata(*metadata);
                    } catch (...) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "documentSaved" && context_) {
                    context_->hostDocuments().saved(message.value("id").toString());
                } else if (command == "documentSaveResult" && context_) {
                    bool valid = false;
                    const auto id = message.value("requestId").toString().toULongLong(&valid);
                    if (!valid || !message.value("saved").isBool() ||
                        !message.value("error").isString()) {
                        qApp->quit();
                        return;
                    }
                    context_->documentRequests().complete(id, message.value("saved").toBool(),
                                                          message.value("error").toString());
                } else if (command == "provideTextDocumentContent" && context_) {
                    bool handleValid = false, requestValid = false;
                    const auto handle =
                        message.value("handle").toString().toULongLong(&handleValid);
                    const auto request =
                        message.value("requestId").toString().toULongLong(&requestValid);
                    const QUrl uri(message.value("uri").toString());
                    if (!handleValid || !requestValid || !handle || !request ||
                        QString::number(handle) != message.value("handle").toString() ||
                        QString::number(request) != message.value("requestId").toString() ||
                        !message.value("uri").isString() || !uri.isValid() ||
                        uri.scheme().isEmpty()) {
                        qApp->quit();
                        return;
                    }
                    context_->contentProviders().provide(handle, request, uri);
                } else if (command == "workspaceResult" && context_) {
                    bool valid = false;
                    const auto encoded = message.value("requestId").toString();
                    const auto id = encoded.toULongLong(&valid);
                    if (!valid || id == 0 || QString::number(id) != encoded ||
                        !message.value("result").isObject() || !message.value("error").isString()) {
                        qApp->quit();
                        return;
                    }
                    context_->workspaceRequests().complete(id, message.value("result").toObject(),
                                                           message.value("error").toString());
                } else if (command == "environmentLogLevel" && context_) {
                    const auto level = message.value("level");
                    if (!level.isDouble() || level.toDouble() != level.toInt(-1) ||
                        level.toInt(-1) < 0 || level.toInt() > 5) {
                        qApp->quit();
                        return;
                    }
                    const QPointer<HostContext> current(context_.get());
                    current->output().setGlobalLogLevel(static_cast<LogLevel>(level.toInt()));
                    if (current)
                        current->hostEnvironment().acceptLogLevel(
                            current->output().globalLogLevel());
                } else if (command == "nativeView" && context_) {
                    if (!context_->nativeViews().accept(message)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "windowState" && context_) {
                    const auto state = wire::decodeWindowState(message.value("state").toObject());
                    if (!state) {
                        qApp->quit();
                        return;
                    }
                    context_->windowState().accept(*state);
                } else if (command == "editorState" && context_) {
                    const auto state = wire::decodeEditorState(message.value("state").toObject());
                    if (!state || !context_->editors().accept(*state)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "environmentShell" && context_) {
                    if (!message.value("shell").isString() ||
                        message.value("shell").toString().size() > 65536) {
                        qApp->quit();
                        return;
                    }
                    context_->hostEnvironment().acceptShell(message.value("shell").toString());
                } else if (command == "externalUriResult" && context_) {
                    if (!context_->hostEnvironment().externalUris().accept(message)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "handleExternalUri" && context_) {
                    if (!context_->uriHandlers().accept(message)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "clipboardResult" && context_) {
                    bool valid = false;
                    const auto encoded = message.value("requestId").toString();
                    const auto id = encoded.toULongLong(&valid);
                    if (!valid || !id || QString::number(id) != encoded ||
                        !message.value("error").isString() || !message.contains("result")) {
                        qApp->quit();
                        return;
                    }
                    context_->hostClipboard().complete(id, message.value("result"),
                                                       message.value("error").toString());
                } else if (command == "progressCancel" && context_) {
                    bool valid = false;
                    const auto encoded = message.value("id").toString();
                    const auto id = encoded.toULongLong(&valid);
                    if (!valid || id == 0 || QString::number(id) != encoded) {
                        qApp->quit();
                        return;
                    }
                    context_->progress().cancel(id);
                } else if (command == "quickPick" && context_) {
                    if (!context_->quickPick().accept(message)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "inputBox" && context_) {
                    if (!context_->inputBox().accept(message)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "windowFileDialogResult" && context_) {
                    bool valid = false;
                    const auto encoded = message.value("requestId").toString();
                    const auto id = encoded.toULongLong(&valid);
                    if (!valid || id == 0 || QString::number(id) != encoded ||
                        !message.value("error").isString() || !message.contains("resources")) {
                        qApp->quit();
                        return;
                    }
                    context_->dialogs().complete(id, message.value("resources"),
                                                 message.value("error").toString());
                } else if (command == "windowMessageResult" && context_) {
                    bool valid = false;
                    const auto encoded = message.value("requestId").toString();
                    const auto id = encoded.toULongLong(&valid);
                    if (!valid || id == 0 || QString::number(id) != encoded ||
                        !message.value("error").isString() || !message.contains("selection")) {
                        qApp->quit();
                        return;
                    }
                    context_->messages().complete(id, message.value("selection"),
                                                  message.value("error").toString());
                } else if (command == "outputClear" && context_) {
                    if (!message.value("id").isString()) {
                        qApp->quit();
                        return;
                    }
                    context_->output().clear(message.value("id").toString());
                } else if (command == "outputLevel" && context_) {
                    const auto level = message.value("level");
                    if (!message.value("id").isString() || !level.isDouble() ||
                        level.toDouble() != level.toInt(-1) || level.toInt(-1) < 0 ||
                        level.toInt() > 5) {
                        qApp->quit();
                        return;
                    }
                    context_->output().setLogLevel(message.value("id").toString(),
                                                   static_cast<LogLevel>(level.toInt()));
                } else if (command == "initialize" && !context_) {
                    if (documentDecoder_.transferring() || documentChangeDecoder_.transferring()) {
                        qApp->quit();
                        return;
                    }
                    const auto configuration =
                        wire::decodeConfigurationModel(message.value("configuration").toObject());
                    const auto environment =
                        wire::decodeEnvironmentInfo(message.value("environment").toObject());
                    const auto windowState =
                        wire::decodeWindowState(message.value("windowState").toObject());
                    const auto editorState =
                        wire::decodeEditorState(message.value("editorState").toObject());
                    if (!configuration || !environment || !windowState || !editorState ||
                        !message.value("workspace").isString()) {
                        qApp->quit();
                        return;
                    }
                    workspace_ = message.value("workspace").toString();
                    activate(*configuration, *environment, *windowState, *editorState);
                    initialDocuments_.clear();
                } else if (command == "configuration" && context_) {
                    auto model = wire::decodeConfigurationModel(message.value("model").toObject());
                    if (!model) {
                        qApp->quit();
                        return;
                    }
                    context_->hostConfiguration().accept(std::move(*model));
                } else if (command == "configurationUpdateResult" && context_) {
                    bool valid = false;
                    const auto id = message.value("requestId").toString().toULongLong(&valid);
                    if (!valid || !message.value("error").isString()) {
                        qApp->quit();
                        return;
                    }
                    context_->hostConfiguration().complete(id, message.value("error").toString());
                } else if (command == "execute" && context_) {
                    const auto id = message.value("id").toString();
                    const auto requestId = message.value("requestId").toString();
                    bool valid = false;
                    requestId.toULongLong(&valid);
                    if (!valid)
                        continue;
                    context_->execute(requestId.toULongLong(), id,
                                      message.value("arguments").toArray());
                } else if (command == "extensionCommandResult" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (valid)
                        context_->completeCommandCall(requestId, message.value("result"),
                                                      message.value("error").toString());
                } else if (command == "getCommandsResult" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (valid)
                        context_->completeCommandList(requestId,
                                                      message.value("commands").toArray(),
                                                      message.value("error").toString());
                } else if (command == "workspace" && context_) {
                    auto model =
                        wire::decodeConfigurationModel(message.value("configuration").toObject());
                    if (!model) {
                        qApp->quit();
                        return;
                    }
                    context_->hostConfiguration().accept(std::move(*model));
                    context_->setWorkspace(message.value("path").toString());
                } else if (command == "format" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (valid)
                        context_->format(requestId, message.value("language").toString(),
                                         message.value("text").toString());
                } else if ((command == "prepareDebugTrackers" ||
                            command == "releaseDebugTrackers" || command == "debugTrackerEvent") &&
                           context_) {
                    bool valid{};
                    const auto text = message.value("requestId").toString();
                    const auto id = text.toULongLong(&valid);
                    if (!valid || !id || QString::number(id) != text) {
                        qApp->quit();
                        return;
                    }
                    if (command == "prepareDebugTrackers") {
                        try {
                            context_->hostDebug().trackers().prepare(
                                id, context_->hostDebug().sessions().session(
                                        message.value("session").toObject()));
                        } catch (...) {
                            qApp->quit();
                            return;
                        }
                    } else if (command == "releaseDebugTrackers")
                        context_->hostDebug().trackers().release(id);
                    else if (!context_->hostDebug().trackers().event(id, message)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "debugStartResult" && context_) {
                    if (!context_->hostDebug().starts().accept(message)) {
                        qApp->quit();
                        return;
                    }
                } else if ((command == "debugSessionEvent" || command == "debugSessionResult") &&
                           context_) {
                    if (!context_->hostDebug().sessions().accept(message)) {
                        qApp->quit();
                        return;
                    }
                } else if (command == "resolveDebugAdapter" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (!valid || !requestId ||
                        QString::number(requestId) != message.value("requestId").toString() ||
                        !message.value("session").isObject()) {
                        qApp->quit();
                        return;
                    }
                    std::optional<DebugAdapterExecutable> executable;
                    if (message.contains("executable")) {
                        const auto decoded = wire::decodeDebugAdapterDescriptor(
                            message.value("executable").toObject());
                        if (!decoded || !std::holds_alternative<DebugAdapterExecutable>(*decoded)) {
                            qApp->quit();
                            return;
                        }
                        executable = std::get<DebugAdapterExecutable>(*decoded);
                    }
                    context_->hostDebug().resolve(requestId, message.value("session").toObject(),
                                                  executable);
                } else if (command == "invokeDebugConfigurationProvider" && context_) {
                    bool valid = false, validHandle = false;
                    const auto idText = message.value("requestId").toString();
                    const auto id = idText.toULongLong(&valid);
                    const auto handleText = message.value("handle").toString();
                    const auto handle = handleText.toULongLong(&validHandle);
                    if (!valid || !id || QString::number(id) != idText || !validHandle || !handle ||
                        QString::number(handle) != handleText) {
                        qApp->quit();
                        return;
                    }
                    std::optional<WorkspaceFolder> folder;
                    if (!message.value("folder").isNull()) {
                        const auto value = message.value("folder").toObject();
                        const QUrl uri(value.value("uri").toString(), QUrl::StrictMode);
                        const auto index = value.value("index");
                        if (!message.value("folder").isObject() || !value.value("uri").isString() ||
                            !uri.isValid() || uri.scheme().isEmpty() ||
                            !value.value("name").isString() || !index.isDouble() ||
                            index.toDouble() != index.toInt(-1) || index.toInt(-1) < 0) {
                            qApp->quit();
                            return;
                        }
                        folder =
                            WorkspaceFolder{uri, value.value("name").toString(), index.toInt()};
                    }
                    context_->hostDebug().configurations().invoke(
                        id, handle, message.value("method").toString(), folder,
                        message.value("configuration"));
                } else if (command == "cancelDebugConfigurationProvider" && context_) {
                    bool valid = false;
                    const auto text = message.value("requestId").toString();
                    const auto id = text.toULongLong(&valid);
                    if (!valid || !id || QString::number(id) != text) {
                        qApp->quit();
                        return;
                    }
                    context_->hostDebug().configurations().cancel(id);
                } else if (command == "releaseDebugAdapter" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (!valid || !requestId ||
                        QString::number(requestId) != message.value("requestId").toString()) {
                        qApp->quit();
                        return;
                    }
                    context_->hostDebug().release(requestId);
                } else if (command == "chatRequest" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (valid && requestId != 0 && message.value("request").isObject() &&
                        message.value("history").isArray())
                        context_->executeChat(requestId, message.value("participantId").toString(),
                                              message.value("request").toObject(),
                                              message.value("history").toArray());
                } else if (command == "chatCancel" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (valid)
                        context_->cancelChat(requestId);
                } else if (command == "chatFeedback" && context_) {
                    if (message.value("result").isObject() && message.value("kind").isDouble())
                        context_->receiveChatFeedback(message.value("participantId").toString(),
                                                      message.value("result").toObject(),
                                                      message.value("kind").toInt(-1));
                } else if (command == "chatFollowupCancel" && context_) {
                    bool valid = false;
                    const auto requestId =
                        message.value("requestId").toString().toULongLong(&valid);
                    if (valid)
                        context_->cancelFollowups(requestId);
                } else if (command == "shutdown") {
                    stop();
                    qApp->quit();
                    return;
                }
            }
        }
    }
    void stop() {
        if (stopping_)
            return;
        // A plugin may spin a nested event loop while activating. Its context
        // must stay alive until activate returns, even if shutdown arrives.
        if (activating_) {
            stopRequested_ = true;
            return;
        }
        stopping_ = true;
        clipboardTransferDecoder_.reset();
        if (std::exchange(active_, false) && plugin_) {
            try {
                plugin_->deactivate();
            } catch (...) {
            }
        }
        if (context_)
            context_->shutdown();
        context_.reset();
        // Native code remains mapped until this process exits.
    }

    QString socketName_, token_, libraryPath_, expectedHash_, extensionId_, workspace_,
        storageRoot_;
    QLocalSocket socket_;
    wire::Decoder decoder_;
    wire::DocumentSnapshotDecoder documentDecoder_;
    wire::DocumentChangeDecoder documentChangeDecoder_;
    wire::DocumentBatchDecoder documentBatchDecoder_;
    wire::JsonMessageTransferDecoder clipboardTransferDecoder_{
        {"command", "clipboardResult", "clipboardResult"}};
    wire::JsonMessageTransferDecoder trackerTransferDecoder_{
        {"command", "debugTrackerEvent", "debugTrackerEvent"}};
    QHash<QString, TextDocumentSnapshot> initialDocuments_;
    std::unique_ptr<QPluginLoader> loader_;
    std::unique_ptr<HostContext> context_;
    Extension* plugin_{};
    bool active_{};
    bool activating_{};
    bool stopRequested_{};
    bool stopping_{};

  public:
    ~ExtensionHost() override { stop(); }
};
} // namespace

int main(int argc, char* argv[]) {
#ifdef Q_OS_MACOS
    // QtKeychain delivers Security framework results on the main dispatch
    // queue. The default console dispatcher does not service that queue.
    qputenv("QT_EVENT_DISPATCHER_CORE_FOUNDATION", "1");
#endif
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addOption({"socket", "Local socket name", "name"});
    parser.addOption({"token", "Connection token", "value"});
    parser.addOption({"plugin", "Plugin library", "path"});
    parser.addOption({"hash", "Trusted SHA-256", "value"});
    parser.addOption({"id", "Extension ID", "value"});
    parser.addOption({"workspace", "Workspace path", "path"});
    parser.addOption({"storage-root", "Extension state directory", "path"});
    parser.process(application);
    for (const auto& option : {"socket", "token", "plugin", "hash", "id", "storage-root"})
        if (parser.value(option).isEmpty())
            return 2;
    ExtensionHost host(parser.value("socket"), parser.value("token"), parser.value("plugin"),
                       parser.value("hash"), parser.value("id"), parser.value("workspace"),
                       parser.value("storage-root"));
    QTimer::singleShot(0, &host, [&host] { host.start(); });
    return application.exec();
}
