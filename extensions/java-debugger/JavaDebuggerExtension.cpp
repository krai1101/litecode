#include "JavaLanguageServer.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

namespace {
using namespace litecode::extensions;
using litecode::java::JavaLanguageServer;

QJsonObject launchConfiguration(const QString& name, const QString& mainClass) {
    return {{"name", name}, {"type", "java"}, {"request", "launch"}, {"mainClass", mainClass}};
}

struct MainClassItem : QuickPickItem {
    QJsonObject entry;
};

class ConfigurationProvider final : public QObject, public DebugConfigurationProvider {
  public:
    ConfigurationProvider(ExtensionContext& context, std::shared_ptr<JavaLanguageServer> server,
                          bool dynamic)
        : context_(context), server_(std::move(server)), dynamic_(dynamic) {}

    ProviderResult<QVector<DebugConfiguration>>
    provideDebugConfigurations(const std::optional<WorkspaceFolder>& folder,
                               CancellationToken) override {
        QVector<DebugConfiguration> result{launchConfiguration("Current File", "${file}")};
        if (dynamic_) {
            if (folder)
                result.append(launchConfiguration("Launch Main Class", ""));
            result.append(QJsonObject{{"name", "Attach to Java Process"},
                                      {"type", "java"},
                                      {"request", "attach"},
                                      {"hostName", "localhost"},
                                      {"port", 5005}});
        }
        return result;
    }

    ProviderResult<DebugConfiguration>
    resolveDebugConfigurationWithSubstitutedVariables(const std::optional<WorkspaceFolder>& folder,
                                                      DebugConfiguration value,
                                                      CancellationToken token) override {
        if (token.isCancellationRequested())
            return DebugConfiguration(QJsonValue::Undefined);
        if (dynamic_)
            return value;
        auto config = value.toObject();
        if (config.value("request") == "attach") {
            const auto port = config.value("port").toInt();
            if (port < 1 || port > 65535)
                throw std::runtime_error("Java attach requires a JDWP port between 1 and 65535.");
            if (!config.contains("hostName"))
                config.insert("hostName", "localhost");
            return DebugConfiguration(config);
        }
        if (config.value("request") != "launch")
            throw std::runtime_error("The Java debugger supports launch and attach requests.");

        const auto main = config.value("mainClass").toString();
        const auto editor = context_.window().activeTextEditor();
        const auto activeFile = editor ? editor->document()->uri().toLocalFile() : QString{};
        const auto file = main.endsWith(".java", Qt::CaseInsensitive) ? main : activeFile;
        const auto workspace = folder ? folder->uri.toLocalFile() : context_.workspacePath();
        const auto root =
            workspace.isEmpty() && !file.isEmpty() ? QFileInfo(file).absolutePath() : workspace;
        if (root.isEmpty())
            throw std::runtime_error("Open a Java file or a Java project first.");
        if (!config.contains("cwd"))
            config.insert("cwd", root);
        if (!config.contains("console"))
            config.insert("console", context_.workspace()
                                         .getConfiguration("java.debug.settings")
                                         ->get("console", "integratedTerminal")
                                         .toString());
        if (config.value("console") != "integratedTerminal" &&
            config.value("console") != "internalConsole")
            throw std::runtime_error(
                "Choose integratedTerminal or internalConsole for Java debugging.");
        auto status = context_.window().setStatusBarMessage("Preparing Java debugger…");
        QFuture<DebugConfiguration> resolved;
        try {
            resolved =
                resolveEntry(root, config, token)
                    .then(this,
                          [this, root, config, main, token](QJsonValue entry) mutable {
                              if (entry.isUndefined() || token.isCancellationRequested())
                                  return QtFuture::makeReadyValueFuture(
                                      DebugConfiguration(QJsonValue::Undefined));
                              const auto selected = entry.toObject();
                              config.insert("mainClass", selected.value("mainClass"));
                              if (selected.value("projectName").isString())
                                  config.insert("projectName", selected.value("projectName"));
                              const auto source = main.endsWith(".java", Qt::CaseInsensitive)
                                                      ? QDir(root).absoluteFilePath(main)
                                                      : QString{};
                              return prepareLaunch(root, config, source, token);
                          })
                    .unwrap();
        } catch (...) {
            status.dispose();
            throw;
        }
        return resolved
            .then([status](DebugConfiguration config) mutable {
                status.dispose();
                return config;
            })
            .onFailed([status](const std::exception& error) mutable -> DebugConfiguration {
                status.dispose();
                throw std::runtime_error(error.what());
            })
            .onCanceled([status]() mutable -> DebugConfiguration {
                status.dispose();
                return QJsonValue::Undefined;
            });
    }

  private:
    QFuture<QJsonValue> resolveEntry(const QString& root, const QJsonObject& config,
                                     CancellationToken token) {
        const auto main = config.value("mainClass").toString();
        const bool sourceFile = main.endsWith(".java", Qt::CaseInsensitive);
        if (!main.isEmpty() && !sourceFile)
            return QtFuture::makeReadyValueFuture(QJsonValue(config));
        if (sourceFile && !QFileInfo(QDir(root).absoluteFilePath(main)).isFile())
            throw std::runtime_error("Save the Java source file before debugging it.");
        const auto project = config.value("projectName").toString();
        const auto uri = QUrl::fromLocalFile(sourceFile ? QDir(root).absoluteFilePath(main) : root)
                             .toString(QUrl::FullyEncoded);
        return server_
            ->execute(root,
                      sourceFile ? "vscode.java.resolveMainMethod" : "vscode.java.resolveMainClass",
                      {sourceFile || project.isEmpty() ? uri : project})
            .then(this,
                  [this, token](QJsonValue value) {
                      if (token.isCancellationRequested())
                          return QtFuture::makeReadyValueFuture(QJsonValue(QJsonValue::Undefined));
                      QVector<MainClassItem> entries;
                      for (const auto& entry : value.toArray()) {
                          const auto object = entry.toObject();
                          const auto name = object.value("mainClass").toString();
                          if (name.isEmpty())
                              continue;
                          MainClassItem item;
                          item.label = name;
                          item.description = object.value("projectName").toString();
                          item.entry = object;
                          entries.append(item);
                      }
                      if (entries.isEmpty())
                          throw std::runtime_error(
                              "No Java main method was found for this launch configuration.");
                      if (entries.size() == 1)
                          return QtFuture::makeReadyValueFuture(QJsonValue(entries.first().entry));
                      QuickPickOptions options;
                      options.placeHolder = "Select a Java main class";
                      return context_.window()
                          .showQuickPick(entries, options, token)
                          .then([](std::optional<QuickPickSelection<MainClassItem>> selection)
                                    -> QJsonValue {
                              if (!selection)
                                  return QJsonValue::Undefined;
                              return std::get<MainClassItem>(*selection).entry;
                          });
                  })
            .unwrap();
    }

    QFuture<DebugConfiguration> prepareLaunch(const QString& root, QJsonObject config,
                                              const QString& source, CancellationToken token) {
        const auto main = config.value("mainClass");
        const auto project = config.value("projectName").toString();
        const auto projectArgument =
            project.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(project);
        auto built = QtFuture::makeReadyValueFuture(QJsonValue(1));
        if (context_.workspace()
                .getConfiguration("java.debug.settings")
                ->get("forceBuildBeforeLaunch", true)
                .toBool()) {
            // The debug plugin skips JDT's default project. Build loose Java
            // files through JDT's document-to-project target before launch.
            if (project.isEmpty() && !source.isEmpty())
                built = server_->buildSourceProject(root, QUrl::fromLocalFile(source));
            else {
                QJsonObject build{{"mainClass", main}, {"isFullBuild", false}};
                if (!project.isEmpty())
                    build.insert("projectName", project);
                built = server_->execute(
                    root, "vscode.java.buildWorkspace",
                    {QString::fromUtf8(QJsonDocument(build).toJson(QJsonDocument::Compact))});
            }
        }
        return built
            .then(this,
                  [this, root, config, main, projectArgument, token](QJsonValue status) {
                      if (token.isCancellationRequested())
                          return QtFuture::makeReadyValueFuture(QJsonValue(QJsonValue::Undefined));
                      if (status.toInt() != 1)
                          throw std::runtime_error(
                              "Java compilation failed. See the Java Debugger output.");
                      if (config.contains("classPaths") || config.contains("modulePaths"))
                          return QtFuture::makeReadyValueFuture(QJsonValue(QJsonValue::Null));
                      return server_->execute(root, "vscode.java.resolveClasspath",
                                              {main, projectArgument, QJsonValue::Null});
                  })
            .unwrap()
            .then(this,
                  [this, root, config, main, projectArgument, token](QJsonValue paths) mutable {
                      if (token.isCancellationRequested())
                          return QtFuture::makeReadyValueFuture(
                              DebugConfiguration(QJsonValue::Undefined));
                      if (paths.isArray()) {
                          const auto list = paths.toArray();
                          config.insert("modulePaths", list.at(0));
                          config.insert("classPaths", list.at(1));
                      }
                      if (config.value("classPaths").toArray().isEmpty() &&
                          config.value("modulePaths").toArray().isEmpty())
                          throw std::runtime_error(
                              "No Java classpath or module path was resolved.");
                      if (!config.value("javaExec").toString().isEmpty())
                          return QtFuture::makeReadyValueFuture(DebugConfiguration(config));
                      return server_
                          ->execute(root, "vscode.java.resolveJavaExecutable",
                                    {main, projectArgument})
                          .then([config](QJsonValue executable) mutable -> DebugConfiguration {
                              if (executable.isString() && !executable.toString().isEmpty())
                                  config.insert("javaExec", executable);
                              return config;
                          });
                  })
            .unwrap();
    }

    ExtensionContext& context_;
    std::shared_ptr<JavaLanguageServer> server_;
    bool dynamic_;
};

class AdapterFactory final : public DebugAdapterDescriptorFactory {
  public:
    AdapterFactory(ExtensionContext& context, std::shared_ptr<JavaLanguageServer> server)
        : context_(context), server_(std::move(server)) {}

    ProviderResult<DebugAdapterDescriptor>
    createDebugAdapterDescriptor(std::shared_ptr<DebugSession> session,
                                 std::optional<DebugAdapterExecutable>) override {
        const auto folder = session->workspaceFolder();
        const auto workspace = folder ? folder->uri.toLocalFile() : context_.workspacePath();
        const auto config = session->configuration();
        const auto launchRoot = serverRoot(config);
        const auto root = workspace.isEmpty() ? launchRoot : workspace;
        return server_->execute(root, "vscode.java.startDebugSession")
            .then([](QJsonValue result) -> DebugAdapterDescriptor {
                const auto port = result.toInt();
                if (port < 1 || port > 65535)
                    throw std::runtime_error(
                        "The Java language server did not return a debug adapter port.");
                return DebugAdapterServer{port, QStringLiteral("127.0.0.1")};
            });
    }

  private:
    QString serverRoot(const QJsonObject& config) const {
        const auto editor = context_.window().activeTextEditor();
        if (editor) {
            const auto path = editor->document()->uri().toLocalFile();
            if (!path.isEmpty())
                return QFileInfo(path).absolutePath();
        }
        return config.value("cwd").toString(QDir::currentPath());
    }

    ExtensionContext& context_;
    std::shared_ptr<JavaLanguageServer> server_;
};

class JavaDebuggerExtension final : public QObject, public Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "extension.json")
    Q_INTERFACES(litecode::extensions::Extension)

  public:
    void activate(ExtensionContext& context) override {
        const auto server = std::make_shared<JavaLanguageServer>(context);
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "java", std::make_shared<ConfigurationProvider>(context, server, false)));
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "java", std::make_shared<ConfigurationProvider>(context, server, true),
            DebugConfigurationProviderTriggerKind::Dynamic));
        context.subscriptions.push_back(context.debug().registerDebugAdapterDescriptorFactory(
            "java", std::make_shared<AdapterFactory>(context, server)));
    }

    void deactivate() override {}
};
} // namespace
#include "JavaDebuggerExtension.moc"
