#pragma once
#include <extensions/UriApi.h>
#include <functional>
namespace litecode::codex {
QUrl codexThreadLink(const QString& scheme, const QString& extensionId, const QString& threadId);
class CodexThreadLinkHandler final : public extensions::UriHandler {
  public:
    CodexThreadLinkHandler(QString scheme, QString extensionId,
                           std::function<void(QString)> open);
    extensions::ProviderResult<void> handleUri(const QUrl& uri) override;
  private:
    QString scheme_, extensionId_;
    std::function<void(QString)> open_;
};
} // namespace litecode::codex
