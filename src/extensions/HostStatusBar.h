#pragma once
#include "extensions/EventApi.h"
#include "extensions/StatusBarApi.h"
#include <QJsonObject>
#include <QObject>
#include <memory>
namespace litecode::extensions {
struct StatusBarRuntime;
class HostStatusBar final : public QObject {
  public:
    HostStatusBar(QString owner, std::function<void(const QJsonObject&)> send);
    ~HostStatusBar() override;
    std::shared_ptr<StatusBarItem> create(std::optional<QString> id, StatusBarAlignment alignment,
                                          std::optional<double> priority);
    Disposable message(const QString& text, std::optional<double> timeout = {});
    void shutdown();

  private:
    std::shared_ptr<StatusBarRuntime> runtime_;
};
} // namespace litecode::extensions
