#include "CodexViewProvider.h"
class CodexExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "extension.json")
    Q_INTERFACES(litecode::extensions::Extension)
  public:
    void activate(litecode::extensions::ExtensionContext& context) override {
        provider_ = std::make_shared<litecode::codex::CodexViewProvider>(context);
        context.subscriptions.push_back(
            context.window().registerViewProvider("litecode.codex.view", provider_));
    }
    void deactivate() override { provider_.reset(); }

  private:
    std::shared_ptr<litecode::codex::CodexViewProvider> provider_;
};
#include "CodexExtension.moc"
