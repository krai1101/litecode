#pragma once
#include "extensions/StatusBarApi.h"
#include "extensions/EventApi.h"
#include <QObject>
#include <QJsonObject>
#include <memory>
namespace litecode::extensions {
struct StatusBarRuntime;
class HostStatusBar final : public QObject {
  public:
    HostStatusBar(QString owner, std::function<void(const QJsonObject&)> send);
    ~HostStatusBar() override;
    std::shared_ptr<StatusBarItem> create(std::optional<QString> id, StatusBarAlignment alignment, std::optional<double> priority);
    Disposable message(const QString& text, std::optional<double> timeout = {});
    void shutdown();
  private:
    std::shared_ptr<StatusBarRuntime> runtime_;
};
}
