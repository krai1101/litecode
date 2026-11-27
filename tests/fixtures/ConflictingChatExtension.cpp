#include "extensions/ExtensionApi.h"

class ConflictingChatExtension final : public QObject, public litecode::extensions::Extension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID LITECODE_EXTENSION_IID FILE "conflicting-chat-extension.json")
    Q_INTERFACES(litecode::extensions::Extension)

  public:
    void activate(litecode::extensions::ExtensionContext&) override {}
    void deactivate() override {}
};

#include "ConflictingChatExtension.moc"
