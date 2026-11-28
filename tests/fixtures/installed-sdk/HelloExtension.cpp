#include <extensions/ExtensionApi.h>

class HelloExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "extension.json")
    Q_INTERFACES(litecode::extensions::Extension)

  public:
    void activate(litecode::extensions::ExtensionContext& context) override {
        context.subscriptions.push_back(
            context.registerDocumentFormatter("markdown", [](const QString& text) {
                return text.isEmpty() || text.endsWith('\n') ? text : text + '\n';
            }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "example.hello.greet", [&context](const QJsonArray&) -> QJsonValue {
                context.showInformationMessage("Hello from a native LiteCode extension!");
                return QJsonValue(QJsonValue::Undefined);
            }));
    }
    void deactivate() override {}
};

#include "HelloExtension.moc"
