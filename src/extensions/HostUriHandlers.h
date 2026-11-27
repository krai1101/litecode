#pragma once
#include "extensions/UriApi.h"
#include "extensions/EventApi.h"
#include <QObject>
#include <QJsonObject>
namespace litecode::extensions {
struct UriHandlerRuntime;
class HostUriHandlers final : public QObject {
  public:
    HostUriHandlers(QString owner, std::function<void(const QJsonObject&)> send);
    ~HostUriHandlers() override;
    Disposable registerUriHandler(std::shared_ptr<UriHandler> handler);
    bool accept(const QJsonObject& message);
    void shutdown();
  private:
    void reportError(const QString& error);
    std::shared_ptr<UriHandlerRuntime> runtime_;
};
}
