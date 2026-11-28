#include "FixturePersistence.h"
#include "extensions/ExtensionApi.h"

class OtherFormatterExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "other-formatter-extension.json")
    Q_INTERFACES(litecode::extensions::Extension)

  public:
    void activate(litecode::extensions::ExtensionContext& context) override {
        registerPersistenceFixture(context);
        context.subscriptions.push_back(context.registerDocumentFormatter(
            "python", [](const QString& text) { return text.toUpper(); }));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.otherFormatter.echo", [](const QJsonArray& arguments) -> QJsonValue {
                return arguments.isEmpty() ? QJsonValue(QJsonValue::Undefined) : arguments.first();
            }));
    }
    void deactivate() override {}
};

#include "OtherFormatterExtension.moc"
