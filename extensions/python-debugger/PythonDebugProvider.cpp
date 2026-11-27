#include "PythonDebugProvider.h"
#include "PythonConfiguration.h"
#include "PythonEnvironment.h"
#include <QDir>
#include <QFileInfo>
namespace litecode::python {
using namespace extensions;
PythonConfigurationProvider::PythonConfigurationProvider(
    ExtensionContext& context, std::shared_ptr<PythonInterpreters> interpreters, bool initial)
    : context_(context), interpreters_(std::move(interpreters)), initial_(initial) {}
ProviderResult<QVector<DebugConfiguration>> PythonConfigurationProvider::provideDebugConfigurations(
    const std::optional<WorkspaceFolder>& folder, CancellationToken token) {
    QVector<DebugConfiguration> result;
    if (token.isCancellationRequested())
        return result;
    for (const auto& configuration : pythonConfigurations(
             folder ? folder->uri.toLocalFile() : context_.workspacePath(), initial_))
        result.append(configuration);
    return result;
}
ProviderResult<DebugConfiguration>
PythonConfigurationProvider::resolveDebugConfiguration(const std::optional<WorkspaceFolder>& folder,
                                                       DebugConfiguration configuration,
                                                       CancellationToken token) {
    if (token.isCancellationRequested())
        return QJsonValue(QJsonValue::Undefined);
    // The dynamic provider supplies choices only. The initial provider owns
    // resolution for both saved and dynamic configurations.
    if (!initial_)
        return configuration;
    const auto editor = context_.window().activeTextEditor();
    const auto file = editor ? editor->document()->uri().toLocalFile() : QString{};
    const auto workspace = folder ? folder->uri.toLocalFile() : context_.workspacePath();
    auto config = preparePythonConfiguration(
        configuration.toObject(), workspace, file,
        context_.workspace().getConfiguration("debugpy")->get("debugJustMyCode", true).toBool(),
        context_.workspace()
            .getConfiguration("python")
            ->get("envFile", "${workspaceFolder}/.env")
            .toString());
    if (config.value("python") == "${command:python.interpreterPath}")
        config.remove("python");
    if (config.contains("module") && config.value("module").toString().isEmpty()) {
        InputBoxOptions options;
        options.title = "Python Module";
        options.prompt = "Module to debug (for example package.main)";
        return context_.window()
            .showInputBox(options, token)
            .then(this,
                  [config](const std::optional<QString>& value) mutable -> DebugConfiguration {
                      if (!value || value->trimmed().isEmpty())
                          return QJsonValue::Undefined;
                      config.insert("module", value->trimmed());
                      return config;
                  });
    }
    return DebugConfiguration(config);
}
ProviderResult<DebugConfiguration>
PythonConfigurationProvider::resolveDebugConfigurationWithSubstitutedVariables(
    const std::optional<WorkspaceFolder>&, DebugConfiguration configuration,
    CancellationToken token) {
    if (token.isCancellationRequested())
        return QJsonValue(QJsonValue::Undefined);
    // The dynamic provider supplies choices only. The initial provider owns
    // resolution for both saved and dynamic configurations.
    if (!initial_)
        return configuration;
    auto config = configuration.toObject();
    if (config.value("request") == "attach" && config.contains("port") &&
        !config.contains("connect")) {
        config.insert("connect", QJsonObject{{"host", config.take("host").toString("127.0.0.1")},
                                             {"port", config.take("port")}});
    }
    if (config.value("request") == "launch") {
        const auto workspace = context_.workspacePath();
        for (const auto* key : {"cwd", "program", "envFile"}) {
            if (!config.contains(key))
                continue;
            if (!config.value(key).isString())
                throw std::runtime_error("Python paths must be strings.");
            const auto path = config.value(key).toString();
            if (!path.isEmpty() && QDir::isRelativePath(path)) {
                const auto base = QString::fromLatin1(key) == "program"
                                      ? config.value("cwd").toString()
                                      : workspace;
                config.insert(key, QDir(base).absoluteFilePath(path));
            }
        }
    }
    validatePythonConfiguration(config);
    if (pythonRemoteConnect(config))
        return DebugConfiguration(config);
    if (config.value("request") == "launch") {
        if (config.contains("env") && !config.value("env").isObject())
            throw std::runtime_error("Python env must be an object.");
        const auto env =
            pythonEnvironment(config.value("envFile").toString(), config.value("env").toObject());
        if (!env.isEmpty())
            config.insert("env", env);
        config.remove("envFile");
    }
    return interpreters_->resolve(config.value("python"), token)
        .then(this, [config](const Interpreter& interpreter) mutable -> DebugConfiguration {
            config.insert("python", interpreter.command());
            return config;
        });
}
PythonAdapterFactory::PythonAdapterFactory(ExtensionContext& context,
                                           std::shared_ptr<PythonInterpreters> interpreters,
                                           std::shared_ptr<LogOutputChannel> output)
    : context_(context), interpreters_(std::move(interpreters)), output_(std::move(output)) {}
ProviderResult<DebugAdapterDescriptor>
PythonAdapterFactory::createDebugAdapterDescriptor(std::shared_ptr<DebugSession> session,
                                                   std::optional<DebugAdapterExecutable>) {
    const auto config = session->configuration();
    validatePythonConfiguration(config);
    if (pythonRemoteConnect(config)) {
        const auto endpoint = config.value("connect").toObject();
        return DebugAdapterDescriptor(
            DebugAdapterServer{endpoint.value("port").toVariant().toInt(),
                               endpoint.value("host").toString("127.0.0.1")});
    }
    const auto python = config.contains("debugAdapterPython") ? config.value("debugAdapterPython")
                                                              : config.value("python");
    return interpreters_->resolve(python).then(
        this, [this, config](const Interpreter& interpreter) -> DebugAdapterDescriptor {
            const auto adapter =
                config.value("debugAdapterPath")
                    .toString(context_.asAbsolutePath("bundled/libs/debugpy/adapter"));
            if (!QFileInfo(adapter).isAbsolute() || !QFileInfo(adapter).exists())
                throw std::runtime_error(
                    "The Python Debugger package is incomplete: debugpy adapter is missing.");
            auto args = interpreter.arguments;
            args.append(adapter);
            if (config.value("logToFile").toBool()) {
                const auto directory = context_.logUri().toLocalFile();
                if (!QDir().mkpath(directory))
                    throw std::runtime_error("Cannot create the Python debug log directory.");
                args.append({"--log-dir", directory});
            }
            output_->info("Starting bundled debugpy with Python " + interpreter.version + ".");
            return DebugAdapterExecutable{interpreter.executable, args, {}};
        });
}
} // namespace litecode::python
