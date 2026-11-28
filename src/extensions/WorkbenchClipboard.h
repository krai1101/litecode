#pragma once
#include <QJsonObject>
#include <QString>
#include <functional>
#include <utility>

namespace litecode::extensions {
// Workbench-thread OS boundary, injected by the Qt UI. The manager remains
// Core-only; headless hosts never construct or access a native clipboard.
class WorkbenchClipboard final {
  public:
    using Reply = std::function<void(const QJsonValue&, const QString&)>;
    void setBackend(std::function<QString()> read, std::function<void(const QString&)> write) {
        read_ = std::move(read);
        write_ = std::move(write);
    }
    bool accept(const QJsonObject& request, const Reply& reply);

  private:
    std::function<QString()> read_;
    std::function<void(const QString&)> write_;
};
} // namespace litecode::extensions
