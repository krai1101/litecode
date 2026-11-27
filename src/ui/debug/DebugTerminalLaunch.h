#pragma once
#include "core/OperationResult.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
namespace litecode::terminal {
class TerminalSessionService;
}
namespace litecode::ui {
// Private terminal readiness boundary; no successful DAP reply before the
// shell starts and the actual terminal accepts the command input.
class DebugTerminalLaunch final : public QObject {
  public:
    DebugTerminalLaunch(terminal::TerminalSessionService* service, core::OperationId id,
                        QString command, bool reused,
                        std::function<void(qint64, const QString&)> complete, QObject* parent);
    ~DebugTerminalLaunch() override;
    void cancel();

  private:
    void submit();
    void finish(qint64 processId, const QString& error);
    QPointer<terminal::TerminalSessionService> service_;
    core::OperationId id_;
    QString command_;
    std::function<void(qint64, const QString&)> complete_;
    QTimer quiet_, deadline_;
    bool reused_{};
    bool started_{}, finished_{};
};
} // namespace litecode::ui
