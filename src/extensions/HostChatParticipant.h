#pragma once
#include "extensions/ChatApi.h"
#include <QJsonValue>
#include <QObject>
#include <memory>
namespace litecode::extensions {
// Private registration facade: public contracts stay in ChatApi.h.
class HostChatParticipant final : public ChatParticipant {
  public:
    using MetadataChanged = std::function<void(const QJsonValue&, bool)>;
    HostChatParticipant(QObject& dispatcher, QString id, ChatRequestHandler handler,
                        std::function<void()> dispose, MetadataChanged metadataChanged);
    ~HostChatParticipant() override;
    QString id() const override;
    ChatRequestHandler requestHandler() const override;
    void requestHandler(ChatRequestHandler handler) override;
    std::optional<IconPath> iconPath() const override;
    void iconPath(std::optional<IconPath> value) override;
    std::shared_ptr<ChatFollowupProvider> followupProvider() const override;
    void followupProvider(std::shared_ptr<ChatFollowupProvider> provider) override;
    Event<ChatResultFeedback> onDidReceiveFeedback() const override;
    void fireFeedback(const ChatResultFeedback& feedback);
    void dispose() override;

  private:
    struct Runtime;
    static void scheduleMetadata(const std::shared_ptr<Runtime>& runtime);
    std::shared_ptr<Runtime> runtime_;
};
} // namespace litecode::extensions
