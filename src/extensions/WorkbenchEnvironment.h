#pragma once
#include "extensions/EnvironmentInfo.h"

namespace litecode::extensions {
// Stores install identity outside plugin-owned state. A process session is shared
// by every manager/window/host; starting/restarting a host never creates a session.
class WorkbenchEnvironment final {
  public:
    explicit WorkbenchEnvironment(const QString& identityFile);
    [[nodiscard]] const EnvironmentInfo& info() const { return info_; }
    [[nodiscard]] const QString& error() const { return error_; }
    bool setShell(const QString& value) {
        if (info_.shell == value) return false;
        info_.shell = value;
        return true;
    }
    void setLogLevel(LogLevel level) { info_.logLevel = level; }
  private:
    EnvironmentInfo info_;
    QString error_;
};
} // namespace litecode::extensions
