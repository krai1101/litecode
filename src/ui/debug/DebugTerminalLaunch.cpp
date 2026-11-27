#include "ui/debug/DebugTerminalLaunch.h"
#include "terminal/TerminalSessionService.h"
#include <utility>
namespace litecode::ui {
DebugTerminalLaunch::DebugTerminalLaunch(terminal::TerminalSessionService* service, core::OperationId id,
    QString command, bool reused, std::function<void(qint64, const QString&)> complete, QObject* parent)
    : QObject(parent), service_(service), id_(id), command_(std::move(command)),
      complete_(std::move(complete)), reused_(reused) {
    quiet_.setSingleShot(true); deadline_.setSingleShot(true);
    connect(&quiet_, &QTimer::timeout, this, &DebugTerminalLaunch::submit);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (started_) submit(); else finish(0, "Debug terminal startup timed out.");
    });
    connect(service, &terminal::TerminalSessionService::sessionStarted, this, [this](core::OperationId id) {
        if (id != id_ || finished_) return;
        started_ = true; deadline_.start(5000);
    });
    connect(service, &terminal::TerminalSessionService::outputReady, this, [this](core::OperationId id, const QByteArray&) {
        if (id == id_ && started_ && !finished_) quiet_.start(500);
    });
    connect(service, &terminal::TerminalSessionService::sessionError, this, [this](core::OperationId id, const QString& error) {
        if (id == id_) finish(0, error);
    });
    connect(service, &terminal::TerminalSessionService::inputRejected, this, [this](core::OperationId id, const QString& error) {
        if (id == id_) finish(0, error);
    });
    connect(service, &terminal::TerminalSessionService::sessionCompleted, this, [this](const terminal::TerminalExit& exit) {
        if (exit.operationId == id_) finish(0, "Debug terminal exited before the command was submitted.");
    });
    connect(service, &terminal::TerminalSessionService::sessionRemoved, this, [this](core::OperationId id) {
        if (id == id_) finish(0, "Debug terminal was closed.");
    });
    if (reused_)
        QTimer::singleShot(0, this, &DebugTerminalLaunch::submit);
    else
        deadline_.start(10000);
}
DebugTerminalLaunch::~DebugTerminalLaunch() {
    if (!finished_) { auto complete = std::exchange(complete_, {}); if (complete) complete(0, "Debug terminal launch was disposed."); }
}
void DebugTerminalLaunch::cancel() {
    if (finished_) return;
    finish(0, "Debug terminal launch was canceled.");
}
void DebugTerminalLaunch::submit() {
    if (finished_) return;
    if (!service_) { finish(0, "Debug terminal service is unavailable."); return; }
    const auto pid = service_->processId(id_);
    const auto bytes = command_.toUtf8() + QByteArrayLiteral("\r");
    const bool accepted = service_->sendInput(id_, bytes);
    if (finished_) return; // Input rejection can be emitted synchronously.
    finish(accepted ? pid : 0, accepted ? QString() : QStringLiteral("Debug terminal rejected the command."));
}
void DebugTerminalLaunch::finish(qint64 pid, const QString& error) {
    if (finished_) return;
    finished_ = true; quiet_.stop(); deadline_.stop();
    auto complete = std::exchange(complete_, {});
    if (!error.isEmpty() && service_ && !reused_) service_->requestRemove(id_);
    if (complete) complete(pid, error);
    deleteLater();
}
} // namespace litecode::ui
