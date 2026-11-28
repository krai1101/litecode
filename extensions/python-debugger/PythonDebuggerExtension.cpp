#include "PythonDebugProvider.h"
#include <QFileInfo>
class PythonDebuggerExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "extension.json")
    Q_INTERFACES(litecode::extensions::Extension)
  public:
    void activate(litecode::extensions::ExtensionContext& context) override {
        using namespace litecode::extensions;
        interpreters_ = std::make_shared<litecode::python::PythonInterpreters>(context);
        output_ =
            context.window().createOutputChannel("Python Debugger", LogOutputChannelOptions{});
        initial_ = std::make_shared<litecode::python::PythonConfigurationProvider>(
            context, interpreters_, true);
        dynamic_ = std::make_shared<litecode::python::PythonConfigurationProvider>(
            context, interpreters_, false);
        factory_ = std::make_shared<litecode::python::PythonAdapterFactory>(context, interpreters_,
                                                                            output_);
        context.subscriptions.push_back(
            context.debug().registerDebugConfigurationProvider("debugpy", initial_));
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "debugpy", dynamic_, DebugConfigurationProviderTriggerKind::Dynamic));
        context.subscriptions.push_back(
            context.debug().registerDebugAdapterDescriptorFactory("debugpy", factory_));
        context.subscriptions.push_back(context.commands().registerCommand(
            "debugpy.triggerEnvSelection", [this, &context](const QJsonArray&) -> CommandResult {
                return interpreters_->select().then(
                    &context,
                    [](const std::optional<litecode::python::Interpreter>& value) -> QJsonValue {
                        return value ? QJsonValue(value->executable)
                                     : QJsonValue(QJsonValue::Undefined);
                    });
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "debugpy.viewOutput", [this](const QJsonArray&) -> CommandResult {
                output_->show();
                return QJsonValue::Undefined;
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "debugpy.debugInTerminal", [&context](const QJsonArray&) -> CommandResult {
                const auto editor = context.window().activeTextEditor();
                if (!editor || editor->document()->isUntitled() ||
                    editor->document()->languageId() != "python")
                    throw std::runtime_error("Open a saved Python file to debug.");
                std::optional<WorkspaceFolder> folder;
                if (!context.workspacePath().isEmpty())
                    folder = WorkspaceFolder{QUrl::fromLocalFile(context.workspacePath()),
                                             QFileInfo(context.workspacePath()).fileName(), 0};
                return context.debug()
                    .startDebugging(folder, QJsonObject{{"name", "Python Debugger: Python File"},
                                                        {"type", "debugpy"},
                                                        {"request", "launch"},
                                                        {"program", editor->document()->fileName()},
                                                        {"console", "integratedTerminal"}})
                    .then(&context, [](bool started) -> QJsonValue { return started; });
            }));
    }
    void deactivate() override {
        factory_.reset();
        dynamic_.reset();
        initial_.reset();
        interpreters_.reset();
        if (output_) {
            output_->dispose();
            output_.reset();
        }
    }

  private:
    std::shared_ptr<litecode::python::PythonInterpreters> interpreters_;
    std::shared_ptr<litecode::python::PythonConfigurationProvider> initial_, dynamic_;
    std::shared_ptr<litecode::python::PythonAdapterFactory> factory_;
    std::shared_ptr<litecode::extensions::LogOutputChannel> output_;
};
#include "PythonDebuggerExtension.moc"
