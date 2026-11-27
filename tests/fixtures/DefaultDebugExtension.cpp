#include "FixtureDebugConfigurationProvider.h"
#include "FixtureDebugTracker.h"
#include "extensions/ExtensionApi.h"
#include <stdexcept>
class DefaultDebugFactory final : public litecode::extensions::DebugAdapterDescriptorFactory {
  public:
    litecode::extensions::ProviderResult<litecode::extensions::DebugAdapterDescriptor>
    createDebugAdapterDescriptor(
        std::shared_ptr<litecode::extensions::DebugSession>,
        std::optional<litecode::extensions::DebugAdapterExecutable> executable) override {
        if (!executable)
            throw std::runtime_error("Missing manifest executable argument.");
        return litecode::extensions::DebugAdapterDescriptor(*executable);
    }
};
// A real separate-host fixture: one type relies entirely on manifest defaults;
// the other explicitly registers a factory which consumes the supplied default.
class DefaultDebugExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "default-debug-extension.json")
    Q_INTERFACES(litecode::extensions::Extension)
  public:
    void activate(litecode::extensions::ExtensionContext& context) override {
        const auto tracker = std::make_shared<QJsonObject>();
        context.subscriptions.push_back(context.debug().registerDebugAdapterTrackerFactory(
            "*", std::make_shared<FixtureDebugTrackerFactory>(tracker)));
        context.subscriptions.push_back(context.commands().registerCommand(
            "tests.defaultDebug.trackerContext",
            [tracker](const QJsonArray&) -> QJsonValue { return *tracker; }));
        context.subscriptions.push_back(context.debug().registerDebugConfigurationProvider(
            "*", std::make_shared<FixtureDebugConfigurationProvider>("default")));
        context.subscriptions.push_back(context.debug().registerDebugAdapterDescriptorFactory(
            "packagedFactory", std::make_shared<DefaultDebugFactory>()));
    }
    void deactivate() override {}
};
#include "DefaultDebugExtension.moc"
