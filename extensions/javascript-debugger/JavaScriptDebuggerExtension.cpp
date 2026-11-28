#include "JsAdapter.h"
#include "extensions/ExtensionApi.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
using namespace litecode::extensions;

class ConfigurationProvider final : public DebugConfigurationProvider {
  public:
    explicit ConfigurationProvider(bool dynamic = false) : dynamic_(dynamic) {}

    ProviderResult<QVector<DebugConfiguration>>
    provideDebugConfigurations(const std::optional<WorkspaceFolder>&, CancellationToken) override {
        QVector<DebugConfiguration> configurations{QJsonObject{{"name", "Launch Program"},
                                                               {"type", "node"},
                                                               {"request", "launch"},
                                                               {"program", "${file}"}}};
        if (dynamic_)
            configurations.append(QJsonObject{{"name", "Attach to Node.js"},
                                              {"type", "node"},
                                              {"request", "attach"},
                                              {"port", 9229}});
        return configurations;
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
        if (config.value("request") == "launch") {
            const auto program = config.value("program").toString();
            const auto cwd = config.value("cwd").toString(
                folder ? folder->uri.toLocalFile() : QFileInfo(program).absolutePath());
            if (program.isEmpty() && config.value("runtimeExecutable").toString().isEmpty())
                throw std::runtime_error("Open a saved JavaScript file or set a launch program.");
            if (!config.contains("console"))
                config.insert("console", "integratedTerminal");
            if (!cwd.isEmpty())
                config.insert("cwd", cwd);
        } else if (config.value("request") != "attach") {
            throw std::runtime_error("The Node.js debugger supports launch and attach requests.");
        }
        // This workbench presents one target per session. js-debug still uses a
        // root session and a separate connection for the primary process.
        const auto root = folder ? folder->uri.toLocalFile() : config.value("cwd").toString();
        config.insert("__workspaceFolder", root);
        config.insert("autoAttachChildProcesses", false);
        return DebugConfiguration(config);
    }

  private:
    bool dynamic_;
};

class AdapterFactory final : public DebugAdapterDescriptorFactory {
  public:
    explicit AdapterFactory(QString entryPoint) : entryPoint_(std::move(entryPoint)) {}

    ProviderResult<DebugAdapterDescriptor>
    createDebugAdapterDescriptor(std::shared_ptr<DebugSession>,
                                 std::optional<DebugAdapterExecutable>) override {
        const auto node = QStandardPaths::findExecutable("node");
        if (node.isEmpty())
            throw std::runtime_error("Install Node.js and make it available on PATH.");
        if (!QFileInfo(entryPoint_).isFile())
            throw std::runtime_error(
                "Reinstall the JavaScript Debugger extension. Its adapter is missing.");
        return DebugAdapterDescriptor(DebugAdapterInlineImplementation{
            std::make_shared<litecode::javascript::JsAdapter>(node, entryPoint_)});
    }

  private:
    QString entryPoint_;
};

class JavaScriptDebuggerExtension final : public QObject, public Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "extension.json")
    Q_INTERFACES(litecode::extensions::Extension)

  public:
    void activate(ExtensionContext& context) override {
        const auto factory = std::make_shared<AdapterFactory>(
            context.asAbsolutePath("bundled/js-debug/src/dapDebugServer.js"));
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "node", std::make_shared<ConfigurationProvider>()));
        context.subscriptions.push_back(
            context.debug().registerDebugAdapterDescriptorFactory("node", factory));
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "node", std::make_shared<ConfigurationProvider>(true),
            DebugConfigurationProviderTriggerKind::Dynamic));
    }

    void deactivate() override {}
};
} // namespace
#include "JavaScriptDebuggerExtension.moc"
