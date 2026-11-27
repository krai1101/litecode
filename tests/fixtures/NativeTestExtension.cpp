#include "FixtureDebugConfigurationProvider.h"
#include "FixtureDebugTracker.h"
#include "FixturePersistence.h"
#include "extensions/ExtensionApi.h"
#include <QCoreApplication>
#include <QFile>
#include <QPromise>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
// The embedded fixture manifest also exercises debugger schema contributions.
class FixtureFollowupProvider final : public litecode::extensions::ChatFollowupProvider {
  public:
    using Result =
        litecode::extensions::ProviderResult<QVector<litecode::extensions::ChatFollowup>>;
    using Callback =
        std::function<Result(litecode::extensions::ChatResult, litecode::extensions::ChatContext,
                             litecode::extensions::CancellationToken)>;
    explicit FixtureFollowupProvider(Callback callback) : callback_(std::move(callback)) {}
    Result provideFollowups(litecode::extensions::ChatResult result,
                            litecode::extensions::ChatContext context,
                            litecode::extensions::CancellationToken token) override {
        return callback_(std::move(result), std::move(context), token);
    }

  private:
    Callback callback_;
};
class FixtureUriHandler final : public litecode::extensions::UriHandler {
  public:
    QJsonArray received;
    litecode::extensions::ProviderResult<void> handleUri(const QUrl& uri) override {
        received.append(uri.toString(QUrl::FullyEncoded));
        return std::monostate{};
    }
};
class FixtureContentProvider final : public litecode::extensions::TextDocumentContentProvider {
  public:
    FixtureContentProvider() { onDidChange = changed.event(); }
    litecode::extensions::ProviderResult<QString>
    provideTextDocumentContent(const QUrl&, litecode::extensions::CancellationToken) override {
        return content;
    }
    QString content{QStringLiteral("provider original")};
    litecode::extensions::EventEmitter<QUrl> changed;
};
class FixtureInlineDebugAdapter final : public litecode::extensions::DebugAdapter {
  public:
    explicit FixtureInlineDebugAdapter(std::shared_ptr<int> disposed)
        : disposed_(std::move(disposed)) {}
    litecode::extensions::Event<QJsonObject> onDidSendMessage() const override {
        return messages_.event();
    }
    void handleMessage(const QJsonObject& request) override {
        const auto command = request.value("command").toString();
        if (request.value("type") == QJsonValue("response") && command == "runInTerminal") {
            terminalResponse_ = request;
            terminalPending_ = false;
            finishLaunch();
            return;
        }
        if (command == "fixtureTerminalResponse") {
            reply(request, terminalResponse_);
            return;
        }
        if (command == "fixtureHang")
            return;
        if (command == "fixtureInvalid") {
            send({{"type", "invalid-message-type"}});
            return;
        }
        if (command == "fixtureEcho") {
            const auto value = request.value("arguments");
            send({{"type", "response"},
                  {"request_seq", request.value("seq")},
                  {"command", command},
                  {"success", true},
                  {"body", value}});
            send({{"type", "event"}, {"event", "fixtureCustom"}, {"body", value}});
            return;
        }
        if (command == "initialize") {
            reply(request, {{"supportsConfigurationDoneRequest", true}});
            return;
        }
        if (command == "launch" || command == "attach") {
            launch_ = request;
            const auto arguments = request.value("arguments").toObject();
            terminalPending_ = arguments.value("terminalFixture").toBool();
            send({{"type", "event"}, {"event", "initialized"}});
            if (terminalPending_)
                send({{"type", "request"},
                      {"command", "runInTerminal"},
                      {"arguments", arguments.value("terminalArguments")}});
            return;
        }
        if (command == "configurationDone") {
            reply(request);
            configured_ = true;
            finishLaunch();
            return;
        }
        if (command == "setBreakpoints") {
            reply(request,
                  {{"breakpoints", request.value("arguments").toObject().value("breakpoints")}});
            return;
        }
        if (command == "pause" || command == "next") {
            reply(request);
            send({{"type", "event"},
                  {"event", "stopped"},
                  {"body", QJsonObject{{"threadId", 1}, {"reason", "step"}}}});
            return;
        }
        if (command == "evaluate") {
            reply(request, {{"result", "42"}, {"variablesReference", 0}});
            return;
        }
        reply(request);
    }
    void dispose() override {
        ++*disposed_;
        messages_.dispose();
    }

  private:
    void finishLaunch() {
        if (!configured_ || terminalPending_ || launchReplied_)
            return;
        launchReplied_ = true;
        send({{"type", "response"},
              {"request_seq", launch_.value("seq")},
              {"command", launch_.value("command")},
              {"success",
               terminalResponse_.isEmpty() || terminalResponse_.value("success").toBool()},
              {"message", terminalResponse_.value("message").toString()},
              {"body", QJsonObject{}}});
    }
    void send(QJsonObject message) {
        message.insert("seq", ++sequence_);
        messages_.fire(message);
    }
    void reply(const QJsonObject& request, const QJsonObject& body = {}) {
        send({{"type", "response"},
              {"request_seq", request.value("seq")},
              {"command", request.value("command")},
              {"success", true},
              {"body", body}});
    }
    litecode::extensions::EventEmitter<QJsonObject> messages_;
    std::shared_ptr<int> disposed_;
    QJsonObject launch_;
    QJsonObject terminalResponse_;
    bool terminalPending_{}, configured_{}, launchReplied_{};
    int sequence_{};
};
class FixtureDebugFactory final : public litecode::extensions::DebugAdapterDescriptorFactory {
  public:
    QJsonObject lastContext;
    std::shared_ptr<litecode::extensions::DebugSession> lastSession;
    std::shared_ptr<int> inlineDisposed = std::make_shared<int>(0);
    litecode::extensions::ProviderResult<litecode::extensions::DebugAdapterDescriptor>
    createDebugAdapterDescriptor(
        std::shared_ptr<litecode::extensions::DebugSession> session,
        std::optional<litecode::extensions::DebugAdapterExecutable> executable) override {
        using namespace litecode::extensions;
        lastSession = session;
        lastContext = {{"id", session->id()},
                       {"type", session->type()},
                       {"name", session->name()},
                       {"configuration", session->configuration()},
                       {"hasExecutable", executable.has_value()}};
        if (const auto folder = session->workspaceFolder())
            lastContext.insert("workspaceFolder", QJsonObject{{"uri", folder->uri.toString()},
                                                              {"name", folder->name},
                                                              {"index", folder->index}});
        const auto config = session->configuration();
        if (config.value("failDescriptor").toBool())
            throw std::runtime_error("fixture factory failure");
        if (config.value("absentDescriptor").toBool())
            return std::optional<DebugAdapterDescriptor>{};
        if (config.value("inlineFixture").toBool())
            return DebugAdapterDescriptor(DebugAdapterInlineImplementation{
                std::make_shared<FixtureInlineDebugAdapter>(inlineDisposed)});
        if (config.contains("serverPort"))
            return DebugAdapterDescriptor(
                DebugAdapterServer{config.value("serverPort").toInt(), {}});
        if (config.contains("pipePath"))
            return DebugAdapterDescriptor(
                DebugAdapterNamedPipeServer{config.value("pipePath").toString()});
        DebugAdapterExecutable descriptor{
            config.value("adapterForTest").toString(),
            {config.value("fixtureSession").toBool() ? QString("--session") : QString("--fixture")},
            {}};
        if (config.contains("adapterEnvironment")) {
            DebugAdapterExecutableOptions options;
            options.env.emplace();
            const auto env = config.value("adapterEnvironment").toObject();
            for (auto it = env.begin(); it != env.end(); ++it)
                options.env->insert(it.key(), it.value().toString());
            if (config.contains("adapterCwd"))
                options.cwd = config.value("adapterCwd").toString();
            descriptor.options = options;
        }
        if (config.value("asyncDescriptor").toBool()) {
            auto promise = std::make_shared<QPromise<DebugAdapterDescriptor>>();
            promise->start();
            const auto future = promise->future();
            QTimer::singleShot(10, QCoreApplication::instance(), [promise, descriptor] {
                promise->addResult(DebugAdapterDescriptor(descriptor));
                promise->finish();
            });
            return future;
        }
        return DebugAdapterDescriptor(descriptor);
    }
};
class NativeTestExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "native-test-extension.json")
    Q_INTERFACES(litecode::extensions::Extension)
  public:
    void activate(litecode::extensions::ExtensionContext& context) override {
        registerPersistenceFixture(context);
        context_ = &context;
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.editorContext", [&context](const QJsonArray&) -> QJsonValue {
                const auto editor = context.window().activeTextEditor();
                if (!editor)
                    return QJsonValue(QJsonValue::Null);
                const auto selection = editor->selection();
                return QJsonObject{{"uri", editor->document()->uri().toString()},
                                   {"text", editor->document()->getText()},
                                   {"version", static_cast<double>(editor->document()->version())},
                                   {"visible", context.window().visibleTextEditors().size()},
                                   {"anchor", QJsonArray{selection.anchor().line(),
                                                         selection.anchor().character()}},
                                   {"active", QJsonArray{selection.active().line(),
                                                         selection.active().character()}}};
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.editorEdit",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                const auto editor = context.window().activeTextEditor();
                if (!editor)
                    throw std::runtime_error("No active editor.");
                const auto position = editor->selection().active();
                const auto argument = arguments.isEmpty() ? QJsonValue{} : arguments.first();
                const auto text = argument.isDouble() ? QString(argument.toInt(), QLatin1Char('X'))
                                                      : argument.toString();
                return editor
                    ->edit(
                        [position, text](const auto& builder) { builder->insert(position, text); })
                    .then([editor](bool applied) -> QJsonValue {
                        return QJsonObject{
                            {"applied", applied},
                            {"length", static_cast<double>(editor->document()->getText().size())},
                            {"active", editor->selection().active().character()}};
                    });
            }));
        windowStates_ = {};
        context.subscriptions.push_back(context.window().onDidChangeWindowState()(
            [this](litecode::extensions::WindowState state) {
                windowStates_.append(
                    QJsonObject{{"focused", state.focused}, {"active", state.active}});
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.windowState", [this, &context](const QJsonArray&) -> QJsonValue {
                const auto state = context.window().state();
                return QJsonObject{{"focused", state.focused},
                                   {"active", state.active},
                                   {"events", windowStates_}};
            }));
        auto uriHandler = std::make_shared<FixtureUriHandler>();
        context.subscriptions.push_back(context.window().registerUriHandler(uriHandler));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.receivedUris",
            [uriHandler](const QJsonArray&) -> QJsonValue { return uriHandler->received; }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.resolveUri",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                const QUrl uri(arguments.first().toString(), QUrl::StrictMode);
                return context.env().asExternalUri(uri).then([](const QUrl& result) -> QJsonValue {
                    return result.toString(QUrl::FullyEncoded);
                });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.statusBarInvoke", [](const QJsonArray& arguments) -> QJsonValue {
                return arguments.isEmpty() ? QJsonValue{} : arguments.first();
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.statusBar", [&context](const QJsonArray&) -> QJsonValue {
                using namespace litecode::extensions;
                auto item =
                    context.window().createStatusBarItem("fixture", StatusBarAlignment::Right, 42);
                item->text("$(zap) Fixture");
                item->name("Fixture status");
                item->tooltip(MarkdownString("**Fixture**"));
                item->command(Command{"Unused title", "tests.native.statusBarInvoke", {}, {42}});
                item->accessibilityInformation(
                    AccessibilityInformation{"Fixture action", QString("button")});
                item->show();
                context.subscriptions.emplace_back([item] { item->dispose(); });
                return QJsonObject{{"id", item->id()},
                                   {"priority", *item->priority()},
                                   {"alignment", static_cast<int>(item->alignment())}};
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.quickPick", [&context](const QJsonArray&) -> QFuture<QJsonValue> {
                using namespace litecode::extensions;
                struct Item : QuickPickItem {
                    int payload{};
                };
                Item first;
                first.label = "Same label";
                first.payload = 11;
                Item second;
                second.label = "Same label";
                second.payload = 22;
                QuickPickOptions options;
                options.title = "Fixture picker";
                return context.window()
                    .showQuickPick(QVector<Item>{first, second}, options)
                    .then([](std::optional<QuickPickSelection<Item>> selection) -> QJsonValue {
                        return selection ? QJsonValue(std::get<Item>(*selection).payload)
                                         : QJsonValue(QJsonValue::Null);
                    });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.inputBox",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                using namespace litecode::extensions;
                InputBoxOptions options;
                options.title = "Fixture input";
                options.value = arguments.isEmpty() ? QString{} : arguments.first().toString();
                options.ignoreFocusOut = true;
                options.validateInput = [](const QString& value) -> InputBoxValidatorResult {
                    auto promise = std::make_shared<QPromise<InputBoxValidationResult>>();
                    promise->start();
                    const auto future = promise->future();
                    QTimer::singleShot(0, [promise, value] {
                        promise->addResult(InputBoxValidationResult(InputBoxValidationMessage{
                            value, value == "error" ? InputBoxValidationSeverity::Error
                                                    : InputBoxValidationSeverity::Warning}));
                        promise->finish();
                    });
                    return future;
                };
                return context.window().showInputBox(options).then(
                    [](std::optional<QString> value) -> QJsonValue {
                        return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
                    });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.primitives", [](const QJsonArray&) -> QJsonValue {
                using namespace litecode::extensions;
                CancellationTokenSource unused;
                unused.dispose();
                unused.cancel();
                const bool fixedNone = !unused.token().isCancellationRequested();
                CancellationTokenSource materialized;
                const auto token = materialized.token();
                int oldCalls = 0, newCalls = 0;
                auto old = token.onCancellationRequested([&] { ++oldCalls; });
                materialized.dispose();
                std::vector<Disposable> subscriptions;
                auto fresh = token.onCancellationRequested([&] { ++newCalls; }, &subscriptions);
                materialized.cancel();
                detail::disposeSubscriptions(subscriptions);
                old.dispose();
                EventEmitter<int> emitter;
                QStringList events;
                auto first = emitter.event()(
                    [&](int value) {
                        events.append("first:" + QString::number(value));
                        if (value == 1)
                            emitter.fire(2);
                    },
                    &subscriptions);
                auto second = emitter.event()(
                    [&](int value) { events.append("second:" + QString::number(value)); },
                    &subscriptions);
                emitter.fire(1);
                detail::disposeSubscriptions(subscriptions);
                return QJsonObject{{"fixedNone", fixedNone},
                                   {"oldCalls", oldCalls},
                                   {"newCalls", newCalls},
                                   {"cancelled", token.isCancellationRequested()},
                                   {"disposedCopies", !fresh && !first && !second},
                                   {"events", QJsonArray::fromStringList(events)}};
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.lifecycle", [this, &context](const QJsonArray& arguments) -> QJsonValue {
                if (arguments.isEmpty() || !arguments.first().isString() ||
                    arguments.first().toString().isEmpty())
                    throw std::invalid_argument("Missing lifecycle fixture marker path.");
                lifecycleFile_ = arguments.at(0).toString();
                throwOnDeactivate_ = arguments.size() > 1 && arguments.at(1).toBool();
                context.subscriptions.emplace_back([this] {
                    lifecycleMarker("subscription:1\n");
                    throw std::runtime_error("fixture cleanup failure");
                });
                context.subscriptions.emplace_back([this] { lifecycleMarker("subscription:2\n"); });
                return true;
            }));
        shellChanges_.clear();
        logLevelChanges_ = QJsonArray{};
        context.subscriptions.push_back(
            context.env().onDidChangeLogLevel()([this](litecode::extensions::LogLevel level) {
                logLevelChanges_.append(static_cast<int>(level));
            }));
        context.subscriptions.push_back(context.env().onDidChangeShell()(
            [this](const QString& path) { shellChanges_.append(path); }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.environment", [this, &context](const QJsonArray&) -> QJsonValue {
                auto& env = context.env();
                QJsonObject result{{"appName", env.appName()},
                                   {"appRoot", env.appRoot()},
                                   {"appHost", env.appHost()},
                                   {"shell", env.shell()},
                                   {"shellChanges", QJsonArray::fromStringList(shellChanges_)},
                                   {"logLevel", static_cast<int>(env.logLevel())},
                                   {"logLevelChanges", logLevelChanges_},
                                   {"language", env.language()},
                                   {"machineId", env.machineId()},
                                   {"sessionId", env.sessionId()},
                                   {"isNewAppInstall", env.isNewAppInstall()},
                                   {"isAppPortable", env.isAppPortable()},
                                   {"uiKind", static_cast<int>(env.uiKind())}};
                if (const auto remote = env.remoteName())
                    result.insert("remoteName", *remote);
                return result;
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.clipboard",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                auto& clipboard = context.env().clipboard();
                if (!arguments.isEmpty() && arguments.first().isObject()) {
                    const auto options = arguments.first().toObject();
                    if (options.value("measure").toBool())
                        return clipboard.readText().then([](const QString& text) -> QJsonValue {
                            return QJsonObject{{"length", static_cast<double>(text.size())},
                                               {"tail", text.right(4)}};
                        });
                    const auto length = options.value("length").toInt();
                    if (length < 0 || length > 16 * 1024 * 1024)
                        throw std::invalid_argument("Invalid fixture clipboard length.");
                    const auto text =
                        QString(length, QLatin1Char('x')) + QString::fromUtf8("\n\xf0\x9f\x98\x80");
                    return clipboard.writeText(text).then([]() -> QJsonValue { return true; });
                }
                if (arguments.isEmpty())
                    return clipboard.readText().then(
                        [](const QString& text) -> QJsonValue { return text; });
                return clipboard.writeText(arguments.first().toString()).then([]() -> QJsonValue {
                    return true;
                });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.applyEdit",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                using namespace litecode::extensions;
                WorkspaceEdit edit;
                for (const auto& argument : arguments) {
                    const auto value = argument.toObject();
                    std::optional<WorkspaceEditEntryMetadata> metadata;
                    if (value.value("needsConfirmation").toBool())
                        metadata = WorkspaceEditEntryMetadata{
                            true, value.value("label").toString("Review extension edit"), {}, {}};
                    edit.insert(QUrl(value.value("uri").toString()), Position(0, 0),
                                value.value("text").toString(), metadata);
                    if (value.contains("eol"))
                        edit.set(QUrl(value.value("uri").toString()),
                                 QVector<TextEdit>{TextEdit::setEndOfLine(
                                     static_cast<EndOfLine>(value.value("eol").toInt()))});
                }
                return context.workspace().applyEdit(edit).then(
                    [](bool applied) -> QJsonValue { return applied; });
            }));
        virtualContent_ = std::make_shared<FixtureContentProvider>();
        context.subscriptions.push_back(context.workspace().registerTextDocumentContentProvider(
            "tests-generated", virtualContent_));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.virtualChange", [this](const QJsonArray& arguments) -> QJsonValue {
                virtualContent_->content = arguments.isEmpty() ? QStringLiteral("provider updated")
                                                               : arguments.first().toString();
                virtualContent_->changed.fire(QUrl("tests-generated:/code.py"));
                return true;
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.openDocument",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                using namespace litecode::extensions;
                QFuture<std::shared_ptr<TextDocument>> opened;
                if (arguments.isEmpty())
                    opened = context.workspace().openTextDocument(UntitledTextDocumentOptions{
                        QString("python"), QString("fixture text"), QString("utf8")});
                else
                    opened =
                        context.workspace().openTextDocument(QUrl(arguments.first().toString()));
                return opened.then(
                    [&context](std::shared_ptr<TextDocument> document) -> QJsonValue {
                        const auto documents = context.workspace().textDocuments();
                        const bool same = std::find(documents.begin(), documents.end(), document) !=
                                          documents.end();
                        return QJsonObject{{"text", document->getText()},
                                           {"uri", document->uri().toString(QUrl::FullyEncoded)},
                                           {"language", document->languageId()},
                                           {"eol", static_cast<int>(document->eol())},
                                           {"sameInstance", same}};
                    });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.progress",
            [this, &context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                using namespace litecode::extensions;
                const auto mode =
                    arguments.isEmpty() ? QStringLiteral("complete") : arguments.first().toString();
                ProgressOptions options;
                options.location = ProgressLocation::Notification;
                options.title = QStringLiteral("Fixture progress");
                options.cancellable = true;
                return context.window().withProgress(
                    options,
                    [this, mode](std::shared_ptr<Progress<ProgressReport>> progress,
                                 CancellationToken token) -> QFuture<QJsonValue> {
                        progress->report({QStringLiteral("Starting"), 10.0});
                        if (mode == "fail")
                            throw std::runtime_error("Fixture progress failure");
                        auto promise = std::make_shared<QPromise<QJsonValue>>();
                        promise->start();
                        auto future = promise->future();
                        const auto finished = std::make_shared<std::atomic<bool>>(false);
                        const auto complete = [promise, finished, progress, token] {
                            if (finished->exchange(true))
                                return;
                            progress->report({QStringLiteral("Finished"), 90.0});
                            promise->addResult(
                                QJsonObject{{"cancelled", token.isCancellationRequested()}});
                            promise->finish();
                        };
                        auto subscription =
                            std::make_shared<Disposable>(token.onCancellationRequested(complete));
                        if (mode == "complete")
                            QTimer::singleShot(0, this, complete);
                        return future.then([subscription](QJsonValue result) {
                            subscription->dispose();
                            return result;
                        });
                    });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.fileDialog",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                using namespace litecode::extensions;
                if (!arguments.isEmpty() && arguments.first().toString() == "save") {
                    SaveDialogOptions options;
                    options.saveLabel = "Fixture Save";
                    options.title = "Save Fixture";
                    return context.window().showSaveDialog(options).then(
                        [](std::optional<QUrl> uri) -> QJsonValue {
                            return uri ? QJsonValue(uri->toString(QUrl::FullyEncoded))
                                       : QJsonValue(QJsonValue::Undefined);
                        });
                }
                OpenDialogOptions options;
                options.canSelectMany = true;
                options.filters =
                    QVector<std::pair<QString, QStringList>>{{"Python", {"py"}}, {"All", {"*"}}};
                return context.window().showOpenDialog(options).then(
                    [](std::optional<QVector<QUrl>> uris) -> QJsonValue {
                        if (!uris)
                            return QJsonValue(QJsonValue::Undefined);
                        QJsonArray result;
                        for (const auto& uri : *uris)
                            result.append(uri.toString(QUrl::FullyEncoded));
                        return result;
                    });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.message", [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                using namespace litecode::extensions;
                const bool modal = !arguments.isEmpty() && arguments.first().toBool();
                return context.window()
                    .showWarningMessage("Fixture message",
                                        MessageOptions{modal, QString("Fixture details")},
                                        QVector<MessageItem>{{"First", false}, {"Close", true}})
                    .then([](std::optional<MessageItem> item) -> QJsonValue {
                        if (!item)
                            return QJsonValue(QJsonValue::Undefined);
                        return QJsonObject{{"title", item->title},
                                           {"close", item->isCloseAffordance.value_or(false)}};
                    });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.output", [this, &context](const QJsonArray& arguments) -> QJsonValue {
                using namespace litecode::extensions;
                const auto operation =
                    arguments.isEmpty() ? QString("create") : arguments.first().toString();
                if (operation == "create") {
                    output_ = context.window().createOutputChannel("Fixture Output");
                    output_->append("before");
                    output_->replace("fixture output");
                    output_->appendLine("");
                    output_->show(true);
                    logOutput_ = context.window().createOutputChannel("Fixture Log",
                                                                      LogOutputChannelOptions{});
                    context.subscriptions.push_back(logOutput_->onDidChangeLogLevel()(
                        [this](LogLevel level) { lastOutputLevel_ = static_cast<int>(level); }));
                    logOutput_->trace("filtered trace");
                    logOutput_->info("fixture info");
                } else if (operation == "level") {
                    return QJsonObject{{"level", static_cast<int>(logOutput_->logLevel())},
                                       {"eventLevel", lastOutputLevel_}};
                } else if (operation == "error") {
                    logOutput_->warn("filtered warning");
                    logOutput_->error("fixture error");
                } else if (operation == "dispose") {
                    output_->dispose();
                    logOutput_->dispose();
                } else if (operation == "closed") {
                    try {
                        output_->append("closed");
                    } catch (const std::runtime_error&) {
                        return true;
                    }
                    return false;
                }
                return QJsonValue(QJsonValue::Undefined);
            }));
        participant_ = context.chat().createChatParticipant(
            "tests.native.assistant",
            [&context](litecode::extensions::ChatRequest request,
                       litecode::extensions::ChatContext history,
                       std::shared_ptr<litecode::extensions::ChatResponseStream> response,
                       litecode::extensions::CancellationToken token)
                -> litecode::extensions::ChatHandlerResult {
                if (request.prompt == "throw")
                    throw std::runtime_error("chat fixture failure");
                response->progress("Working");
                response->markdown(QString("history=%1 prompt=%2")
                                       .arg(history.history.size())
                                       .arg(request.prompt));
                if (request.prompt == "ui-stream") {
                    using namespace litecode::extensions;
                    auto promise = std::make_shared<QPromise<ChatResult>>();
                    promise->start();
                    const auto future = promise->future();
                    response->markdown("\n**stream");
                    QTimer::singleShot(20, [response, token] {
                        if (!token.isCancellationRequested())
                            response->markdown("ed** response");
                    });
                    QTimer::singleShot(120, [response, token, promise, command = request.command] {
                        try {
                            if (token.isCancellationRequested())
                                throw CancellationError();
                            response->markdown(".");
                            promise->addResult(
                                {{}, QJsonObject{{"command", command.value_or(QString{})}}});
                        } catch (...) {
                            promise->setException(std::current_exception());
                        }
                        promise->finish();
                    });
                    return future;
                }
                if (request.prompt == "ui-resources") {
                    using namespace litecode::extensions;
                    const auto base = QUrl::fromLocalFile(context.workspacePath() + "/");
                    const auto uri = base.resolved(QUrl("fixture.txt"));
                    const Location location{uri, {{2, 3}, {2, 9}}};
                    response->anchor(location, "Fixture location");
                    response->reference(location);
                    response->filetree({{"fixture.txt", {}}}, base);
                    response->button({"Hello", "tests.native.hello"});
                    return ChatResult{};
                }
                if (request.prompt == "absentResult")
                    return std::optional<litecode::extensions::ChatResult>{};
                if (request.prompt == "optionalResult")
                    return std::optional<litecode::extensions::ChatResult>{
                        litecode::extensions::ChatResult{{}, QJsonObject{{"optional", true}}}};
                if (request.prompt == "asyncAbsentResult" ||
                    request.prompt == "asyncOptionalResult") {
                    using Result = litecode::extensions::ChatResult;
                    auto promise = std::make_shared<QPromise<std::optional<Result>>>();
                    promise->start();
                    const auto future = promise->future();
                    QTimer::singleShot(0, [promise, token, prompt = request.prompt] {
                        if (token.isCancellationRequested())
                            promise->setException(
                                std::make_exception_ptr(litecode::extensions::CancellationError()));
                        else
                            promise->addResult(prompt == "asyncAbsentResult"
                                                   ? std::optional<Result>{}
                                                   : std::optional<Result>{Result{
                                                         {}, QJsonObject{{"optional", true}}}});
                        promise->finish();
                    });
                    return future;
                }
                if (request.prompt == "typed") {
                    using namespace litecode::extensions;
                    MarkdownString markdown{"[source](./fixture.txt)"};
                    markdown.isTrusted =
                        MarkdownStringTrustedOptions{QStringList{"tests.native.hello"}};
                    markdown.supportThemeIcons = true;
                    markdown.baseUri = QUrl("file:///workspace/");
                    response->push(ChatResponseMarkdownPart{markdown});
                    const Location location{QUrl("file:///fixture.txt"), {{2, 3}, {2, 9}}};
                    response->push(ChatResponseAnchorPart{location, QString("Located")});
                    response->push(ChatResponseCommandButtonPart{Command{
                        "Hello", "tests.native.hello", QString("Run hello"), QJsonArray{42}}});
                    response->push(ChatResponseFileTreePart{
                        {{"folder",
                          QVector<ChatResponseFileTree>{
                              {"empty", QVector<ChatResponseFileTree>{}}}}},
                        QUrl("file:///workspace")});
                    response->push(ChatResponseReferencePart{
                        location, IconPath{ThemeIcon{"file", ThemeColor{QString("charts.blue")}}}});
                    return ChatResult{};
                }
                if (request.prompt == "#a #b") {
                    QJsonArray referenceIds;
                    for (const auto& reference : request.references)
                        referenceIds.append(reference.id);
                    QJsonArray toolNames;
                    for (const auto& tool : request.toolReferences)
                        toolNames.append(tool.name);
                    const auto& first = request.references.first();
                    const auto& second = request.references.at(1);
                    return litecode::extensions::ChatResult{
                        {},
                        QJsonObject{
                            {"referenceIds", referenceIds},
                            {"toolNames", toolNames},
                            {"firstRangeStart", first.range->first},
                            {"firstIsLocation",
                             std::holds_alternative<litecode::extensions::Location>(first.value)},
                            {"secondIsUri", std::holds_alternative<QUrl>(second.value)},
                            {"attachedValue",
                             std::get<QJsonValue>(request.references.last().value)}}};
                }
                if (request.prompt == "history-check") {
                    using namespace litecode::extensions;
                    const auto* firstRequest =
                        history.history.size() >= 2
                            ? std::get_if<ChatRequestTurn>(&history.history.at(0))
                            : nullptr;
                    const auto* firstResponse =
                        history.history.size() >= 2
                            ? std::get_if<ChatResponseTurn>(&history.history.at(1))
                            : nullptr;
                    const auto* markdown =
                        firstResponse && !firstResponse->response.isEmpty()
                            ? std::get_if<ChatResponseMarkdownPart>(&firstResponse->response.at(0))
                            : nullptr;
                    return ChatResult{
                        {},
                        QJsonObject{
                            {"requestTyped", firstRequest != nullptr},
                            {"responseTyped", firstResponse != nullptr},
                            {"firstPrompt", firstRequest ? firstRequest->prompt : QString{}},
                            {"firstCommand",
                             firstRequest ? firstRequest->command.value_or(QString{}) : QString{}},
                            {"firstReference", firstRequest && !firstRequest->references.isEmpty()
                                                   ? firstRequest->references.first().id
                                                   : QString{}},
                            {"contentCount", firstResponse ? firstResponse->response.size() : 0},
                            {"firstMarkdown", markdown ? markdown->value.value : QString{}},
                            {"firstResultCommand",
                             firstResponse
                                 ? firstResponse->result.metadata.value("command").toString()
                                 : QString{}}}};
                }
                if (request.prompt == "late") {
                    QTimer::singleShot(0, [response, &context] {
                        try {
                            response->markdown("late write");
                        } catch (const litecode::extensions::CancellationError&) {
                            context.showInformationMessage("Late chat write rejected");
                        }
                    });
                    return litecode::extensions::ChatResult{};
                }
                if (request.prompt == "async" || request.prompt == "cancel") {
                    if (request.prompt == "cancel")
                        static_cast<void>(token.onCancellationRequested([&context] {
                            context.showInformationMessage("Chat cancellation observed");
                        }));
                    auto promise = std::make_shared<QPromise<litecode::extensions::ChatResult>>();
                    promise->start();
                    const auto future = promise->future();
                    const auto copy = token;
                    QTimer::singleShot(request.prompt == "cancel" ? 250 : 20, [promise, copy] {
                        if (copy.isCancellationRequested()) {
                            promise->setException(
                                std::make_exception_ptr(litecode::extensions::CancellationError()));
                        } else {
                            promise->addResult({{}, QJsonObject{{"async", true}}});
                        }
                        promise->finish();
                    });
                    return future;
                }
                response->anchor(QUrl("file:///fixture.txt"), QString("Fixture"));
                response->reference(QUrl("file:///fixture.txt"));
                response->button({"Hello", "tests.native.hello"});
                response->filetree({{"fixture.txt", {}}}, QUrl("file:///workspace"));
                if (request.prompt == "ui-filtered")
                    return litecode::extensions::ChatResult{
                        litecode::extensions::ChatErrorDetails{"Filtered fixture response", true},
                        {}};
                return litecode::extensions::ChatResult{
                    {},
                    QJsonObject{{"command", request.command.value_or(QString{})},
                                {"references", request.references.size()},
                                {"followupForm", request.prompt},
                                {"foreignFollowup", request.prompt == "foreignFollowup"},
                                {"slowFollowup", request.prompt == "slowFollowup"}}};
            });
        participant_->iconPath(
            litecode::extensions::IconPath{litecode::extensions::LightDarkIconPath{
                QUrl("file:///fixture-light.svg"), QUrl("file:///fixture-dark.svg")}});
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.replaceParticipantHandler", [this](const QJsonArray&) -> QJsonValue {
                participant_->requestHandler(
                    [](litecode::extensions::ChatRequest request, litecode::extensions::ChatContext,
                       std::shared_ptr<litecode::extensions::ChatResponseStream> response,
                       litecode::extensions::CancellationToken)
                        -> litecode::extensions::ChatHandlerResult {
                        response->markdown("replacement:" + request.prompt);
                        return litecode::extensions::ChatResult{{},
                                                                QJsonObject{{"replaced", true}}};
                    });
                participant_->iconPath(
                    litecode::extensions::IconPath{QUrl("file:///replacement.svg")});
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.themeParticipantIcon", [this](const QJsonArray&) -> QJsonValue {
                participant_->iconPath(
                    litecode::extensions::IconPath{litecode::extensions::ThemeIcon{"chat", {}}});
                return QJsonValue(QJsonValue::Undefined);
            }));
        participant_->followupProvider(std::make_shared<FixtureFollowupProvider>(
            [](litecode::extensions::ChatResult result, litecode::extensions::ChatContext,
               litecode::extensions::CancellationToken token) -> FixtureFollowupProvider::Result {
                const QVector<litecode::extensions::ChatFollowup> values{
                    {"Ask another question", QString("Ask again"),
                     QString("tests.native.assistant"), QString("explain")}};
                const auto form = result.metadata.value("followupForm").toString();
                if (form == "absentFollowups")
                    return std::optional<QVector<litecode::extensions::ChatFollowup>>{};
                if (form == "optionalFollowups")
                    return std::optional<QVector<litecode::extensions::ChatFollowup>>{values};
                if (form == "asyncAbsentFollowups" || form == "asyncOptionalFollowups") {
                    using Items = QVector<litecode::extensions::ChatFollowup>;
                    auto promise = std::make_shared<QPromise<std::optional<Items>>>();
                    promise->start();
                    const auto future = promise->future();
                    QTimer::singleShot(0, [promise, token, form, values] {
                        if (token.isCancellationRequested())
                            promise->setException(
                                std::make_exception_ptr(litecode::extensions::CancellationError()));
                        else
                            promise->addResult(form == "asyncAbsentFollowups"
                                                   ? std::optional<Items>{}
                                                   : std::optional<Items>{values});
                        promise->finish();
                    });
                    return future;
                }
                if (result.metadata.value("foreignFollowup").toBool())
                    return QVector<litecode::extensions::ChatFollowup>{
                        {"Cross-extension", {}, QString("another.extension.chat"), {}}};
                const bool slow = result.metadata.value("slowFollowup").toBool();
                if (!result.metadata.value("async").toBool() && !slow)
                    return values;
                auto promise =
                    std::make_shared<QPromise<QVector<litecode::extensions::ChatFollowup>>>();
                promise->start();
                const auto future = promise->future();
                QTimer::singleShot(slow ? 1000 : 20, [promise, values, token] {
                    if (token.isCancellationRequested())
                        promise->setException(
                            std::make_exception_ptr(litecode::extensions::CancellationError()));
                    else
                        promise->addResult(values);
                    promise->finish();
                });
                return future;
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.participantProperties", [this](const QJsonArray&) -> QJsonValue {
                return QJsonObject{{"id", participant_->id()},
                                   {"requestHandler", bool(participant_->requestHandler())},
                                   {"followupProvider", bool(participant_->followupProvider())},
                                   {"iconPath", bool(participant_->iconPath())}};
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.clearFollowupProvider", [this](const QJsonArray&) -> QJsonValue {
                participant_->followupProvider({});
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(participant_->onDidReceiveFeedback()(
            [&context](const litecode::extensions::ChatResultFeedback& feedback) {
                context.showInformationMessage(
                    QString("Chat feedback %1 command=%2")
                        .arg(static_cast<int>(feedback.kind))
                        .arg(feedback.result.metadata.value("command").toString()));
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.disposeParticipant", [this](const QJsonArray&) -> QJsonValue {
                if (participant_)
                    participant_->dispose();
                return QJsonValue(QJsonValue::Undefined);
            }));
        const auto debugFactory = std::make_shared<FixtureDebugFactory>();
        const auto typedTracker = std::make_shared<QJsonObject>();
        const auto wildcardTracker = std::make_shared<QJsonObject>();
        context.subscriptions.push_back(context.debug().registerDebugAdapterTrackerFactory(
            "fixture", std::make_shared<FixtureDebugTrackerFactory>(typedTracker)));
        context.subscriptions.push_back(context.debug().registerDebugAdapterTrackerFactory(
            "*", std::make_shared<FixtureDebugTrackerFactory>(wildcardTracker)));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.debugTrackerContext",
            [typedTracker, wildcardTracker](const QJsonArray&) -> QJsonValue {
                return QJsonObject{{"typed", *typedTracker}, {"wildcard", *wildcardTracker}};
            }));
        const auto sessionEvents = std::make_shared<QJsonObject>();
        const auto record = [sessionEvents](const QString& key) {
            sessionEvents->insert(key, sessionEvents->value(key).toInt() + 1);
        };
        context.subscriptions.push_back(context.debug().onDidStartDebugSession()(
            [record, debugFactory, sessionEvents](const auto& session) {
                record("started");
                sessionEvents->insert("sameIdentity", session == debugFactory->lastSession);
            }));
        context.subscriptions.push_back(context.debug().onDidTerminateDebugSession()(
            [record](const auto&) { record("terminated"); }));
        context.subscriptions.push_back(context.debug().onDidChangeActiveDebugSession()(
            [record](const auto&) { record("activeChanges"); }));
        context.subscriptions.push_back(context.debug().onDidReceiveDebugSessionCustomEvent()(
            [sessionEvents, record](const auto& event) {
                record("custom");
                sessionEvents->insert("customBody", event.body);
                sessionEvents->insert("customEvent", event.event);
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.debugSessionContext",
            [&context, sessionEvents](const QJsonArray&) -> QJsonValue {
                auto value = *sessionEvents;
                const auto session = context.debug().activeDebugSession();
                value.insert("active", bool(session));
                if (session) {
                    value.insert("id", session->id());
                    value.insert("name", session->name());
                }
                return value;
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.debugSessionRequest",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                const auto session = context.debug().activeDebugSession();
                if (!session)
                    throw std::runtime_error("No active debug session.");
                return session->customRequest(
                    arguments.isEmpty() ? QString() : arguments[0].toString(),
                    arguments.size() > 1 ? arguments[1] : QJsonValue(QJsonValue::Undefined));
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.debugSessionRename",
            [&context](const QJsonArray& arguments) -> QJsonValue {
                const auto session = context.debug().activeDebugSession();
                if (!session)
                    throw std::runtime_error("No active debug session.");
                session->name(arguments.isEmpty() ? QString() : arguments[0].toString());
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.debugSessionStop", [&context](const QJsonArray&) -> QFuture<QJsonValue> {
                return context.debug().stopDebugging().then(
                    [] { return QJsonValue(QJsonValue::Undefined); });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.debugStart",
            [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                std::optional<litecode::extensions::WorkspaceFolder> folder;
                if (arguments.size() > 1 && arguments[1].isString()) {
                    const QUrl uri(arguments[1].toString());
                    folder = litecode::extensions::WorkspaceFolder{uri, "Fixture workspace", 0};
                }
                std::variant<QString, QJsonObject> configuration;
                if (!arguments.isEmpty() && arguments[0].isString())
                    configuration = arguments[0].toString();
                else
                    configuration = arguments.isEmpty() ? QJsonObject{} : arguments[0].toObject();
                litecode::extensions::DebugSessionOptions options;
                const auto values = arguments.size() > 2 ? arguments[2].toObject() : QJsonObject{};
                if (values.contains("noDebug"))
                    options.noDebug = values.value("noDebug").toBool();
                options.suppressSaveBeforeStart = true;
                options.suppressDebugView = true;
                options.suppressDebugToolbar = values.value("suppressDebugToolbar").toBool();
                return context.debug()
                    .startDebugging(folder, configuration, options)
                    .then([](bool started) { return QJsonValue(started); });
            }));
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "fixture", std::make_shared<FixtureDebugConfigurationProvider>("native", true)));
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "fixture", std::make_shared<FixtureDynamicDebugConfigurationProvider>(),
            litecode::extensions::DebugConfigurationProviderTriggerKind::Dynamic));
        context.subscriptions.push_back(
            context.debug().registerDebugAdapterDescriptorFactory("fixture", debugFactory));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.debugFactoryContext", [debugFactory](const QJsonArray&) -> QJsonValue {
                auto value = debugFactory->lastContext;
                value.insert("inlineDisposed", *debugFactory->inlineDisposed);
                return value;
            }));
        context.subscriptions.push_back(context.registerDocumentFormatter(
            "python", [](const QString& text) { return text.trimmed() + '\n'; }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.hostPid", [&context](const QJsonArray&) -> QJsonValue {
                context.showInformationMessage(QString::number(QCoreApplication::applicationPid()));
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.workspace", [&context](const QJsonArray&) -> QJsonValue {
                context.showInformationMessage(context.workspacePath());
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.extensionLocation",
            [&context](const QJsonArray& arguments) -> QJsonValue {
                const auto relative = arguments.isEmpty()
                                          ? QStringLiteral("native-test-extension.json")
                                          : arguments.first().toString();
                return QJsonObject{{"id", context.extensionId()},
                                   {"path", context.extensionPath()},
                                   {"uri", context.extensionUri().toString()},
                                   {"resource", context.asAbsolutePath(relative)}};
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.exitHost", [](const QJsonArray&) -> QJsonValue { std::_Exit(55); }));
        context.subscriptions.push_back(
            context.registerCommand("tests.native.throw", [](const QJsonArray&) -> QJsonValue {
                throw std::runtime_error("fixture failure");
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.blockHost", [](const QJsonArray& arguments) -> QJsonValue {
                // Socket notifications may stay buffered while the event loop
                // is blocked on Unix. Publish a private marker before blocking
                // so the parent can prove it disabled an actually busy host.
                QFile marker(arguments.isEmpty() ? QString() : arguments.first().toString());
                if (!marker.open(QIODevice::WriteOnly) || marker.write("blocked") != 7)
                    throw std::runtime_error("Could not publish the busy-host fixture marker.");
                marker.close();
                QThread::sleep(30);
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.echo", [](const QJsonArray& arguments) -> QJsonValue {
                return arguments.isEmpty() ? QJsonValue(QJsonValue::Undefined) : arguments.first();
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.future", [](const QJsonArray&) -> QFuture<QJsonValue> {
                QPromise<QJsonValue> promise;
                promise.start();
                promise.addResult(QJsonValue("future result"));
                promise.finish();
                return promise.future();
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.bridge", [&context](const QJsonArray& arguments) -> QFuture<QJsonValue> {
                return context.commands().executeCommand("tests.otherFormatter.echo", arguments);
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.commandList", [&context](const QJsonArray&) -> QFuture<QJsonValue> {
                return context.commands().getCommands(true).then([](const QStringList& ids) {
                    return QJsonValue(QJsonArray::fromStringList(ids));
                });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.missingCommand", [&context](const QJsonArray&) -> QFuture<QJsonValue> {
                return context.commands().executeCommand("tests.noSuchExtension.command");
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.invokeWorkbench", [&context](const QJsonArray&) -> QFuture<QJsonValue> {
                return context.commands().executeCommand("workbench.fixture");
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.invokeWorkbenchWithArgs",
            [&context](const QJsonArray&) -> QFuture<QJsonValue> {
                return context.commands().executeCommand("workbench.fixture", {"unexpected"});
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.native.commandListAll", [&context](const QJsonArray&) -> QFuture<QJsonValue> {
                return context.commands().getCommands(false).then([](const QStringList& ids) {
                    return QJsonValue(QJsonArray::fromStringList(ids));
                });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "_tests.native.internal",
            [](const QJsonArray&) -> QJsonValue { return QJsonValue(QJsonValue::Undefined); }));
        // Like VS Code, losing the Disposable handle must not unregister a command.
        static_cast<void>(context.registerCommand(
            "tests.native.unretained", [&context](const QJsonArray&) -> QJsonValue {
                context.showInformationMessage("Unretained command invoked");
                return QJsonValue(QJsonValue::Undefined);
            }));
        temporary_ = context.registerCommand(
            "tests.native.temporary", [&context](const QJsonArray&) -> QJsonValue {
                context.showInformationMessage("Temporary command invoked");
                return QJsonValue(QJsonValue::Undefined);
            });
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.disposeTemporary", [this](const QJsonArray&) -> QJsonValue {
                temporary_.dispose();
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.hello", [&context](const QJsonArray&) -> QJsonValue {
                context.showInformationMessage("Hello from native C++");
                return QJsonValue(QJsonValue::Undefined);
            }));
        context.subscriptions.push_back(context.registerCommand(
            "tests.native.duplicateCheck", [&context](const QJsonArray&) -> QJsonValue {
                try {
                    static_cast<void>(context.registerCommand(
                        "tests.native.hello", [](const QJsonArray&) -> QJsonValue {
                            return QJsonValue(QJsonValue::Undefined);
                        }));
                } catch (const std::invalid_argument&) {
                    return QJsonValue("duplicate rejected");
                }
                return QJsonValue("duplicate accepted");
            }));
    }
    void deactivate() override {
        if (!lifecycleFile_.isEmpty()) {
            lifecycleMarker("deactivate\n");
            try {
                auto registration = context_->commands().registerCommand(
                    "tests.native.duringDeactivate",
                    [](const QJsonArray&) -> QJsonValue { return true; });
                registration.dispose();
                lifecycleMarker("context:available\n");
            } catch (...) {
                lifecycleMarker("context:unavailable\n");
            }
            if (throwOnDeactivate_)
                throw std::runtime_error("fixture deactivate failure");
        }
        if (output_)
            output_->dispose();
        if (logOutput_)
            logOutput_->dispose();
        if (participant_)
            participant_->dispose();
        temporary_.dispose();
    }

  private:
    void lifecycleMarker(const QByteArray& text) {
        QFile file(lifecycleFile_);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(text) != text.size())
            throw std::runtime_error("Could not write lifecycle fixture marker.");
    }
    litecode::extensions::ExtensionContext* context_{};
    QString lifecycleFile_;
    bool throwOnDeactivate_{};
    QStringList shellChanges_;
    QJsonArray logLevelChanges_;
    QJsonArray windowStates_;
    std::shared_ptr<FixtureContentProvider> virtualContent_;
    std::shared_ptr<litecode::extensions::OutputChannel> output_;
    std::shared_ptr<litecode::extensions::LogOutputChannel> logOutput_;
    int lastOutputLevel_{-1};
    litecode::extensions::Disposable temporary_;
    std::shared_ptr<litecode::extensions::ChatParticipant> participant_;
};
#include "NativeTestExtension.moc"
