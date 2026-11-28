#include "extensions/ExtensionApi.h"

class ConflictingCommandExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "conflicting-command-extension.json")
    Q_INTERFACES(litecode::extensions::Extension)

  public:
    void activate(litecode::extensions::ExtensionContext& context) override {
        context.subscriptions.push_back(
            context.registerCommand("tests.native.hello", [](const QJsonArray&) -> QJsonValue {
                return QJsonValue("conflicting extension");
            }));
    }
    void deactivate() override {}
};

#include "ConflictingCommandExtension.moc"
