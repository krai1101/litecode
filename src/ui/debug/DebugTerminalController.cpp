#include "ui/debug/DebugTerminalController.h"
#include "ui/RunDebugPanel.h"
#include "ui/TerminalPanel.h"
#include <QFileInfo>
#include <QJsonArray>
#include <utility>
namespace litecode::ui {
DebugTerminalController::DebugTerminalController(RunDebugPanel* debug, TerminalPanel* terminal,
                                                 std::function<QString()> workspace,
                                                 QObject* parent)
    : QObject(parent), debug_(debug), terminal_(terminal), workspace_(std::move(workspace)) {
    connect(debug, &RunDebugPanel::runInTerminalRequested, this, &DebugTerminalController::launch);
    connect(terminal, &TerminalPanel::debugCommandReady, this, &DebugTerminalController::complete);
    connect(debug, &RunDebugPanel::debugSessionEvent, this, [this](const QJsonObject& event) {
        if (event.value("kind") == QJsonValue("terminated") ||
            event.value("kind") == QJsonValue("discarded"))
            cancelPending();
    });
    connect(debug, &QObject::destroyed, this, [this] { cancelPending(); });
}
DebugTerminalController::~DebugTerminalController() { cancelPending(); }
void DebugTerminalController::cancelPending() {
    const auto pending = std::exchange(pending_, {});
    const auto leased = std::exchange(leased_, {});
    if (!terminal_)
        return;
    for (auto id = pending.begin(); id != pending.end(); ++id)
        terminal_->cancelDebugCommand(core::OperationId(id.key()));
    for (const auto id : leased)
        terminal_->releaseDebugTerminal(id);
}
void DebugTerminalController::complete(core::OperationId id, qint64 pid, const QString& error) {
    const auto found = pending_.find(id.value());
    if (found == pending_.end())
        return;
    const auto request = found.value();
    pending_.erase(found);
    QJsonObject body;
    if (pid > 0)
        body.insert("shellProcessId", static_cast<double>(pid));
    if (debug_)
        debug_->respondToRunInTerminal(request, error.isEmpty(), error, body);
}
void DebugTerminalController::launch(const QJsonObject& request) {
    if (!debug_)
        return;
    const auto reject = [this, &request](const QString& error) {
        if (debug_)
            debug_->respondToRunInTerminal(request, false, error);
    };
    if (!terminal_) {
        reject("Integrated terminal is unavailable.");
        return;
    }
    const auto arguments = request.value("arguments").toObject();
    const auto kind = arguments.value("kind");
    const auto interpret = arguments.value("argsCanBeInterpretedByShell");
    if (!request.value("arguments").isObject() ||
        (!kind.isUndefined() && kind != QJsonValue("integrated")) ||
        (!interpret.isUndefined() && !interpret.isBool()) || !arguments.value("args").isArray() ||
        !arguments.value("cwd").isString() ||
        (!arguments.value("title").isUndefined() && !arguments.value("title").isString())) {
        reject("Invalid or unsupported debug terminal request.");
        return;
    }
    QStringList command;
    for (const auto& argument : arguments.value("args").toArray()) {
        if (!argument.isString() || argument.toString().contains(QChar('\0'))) {
            reject("Invalid debug terminal argument.");
            return;
        }
        command.append(argument.toString());
    }
    if (command.isEmpty() || command.first().isEmpty()) {
        reject("Debug terminal command is empty.");
        return;
    }
    auto cwd = arguments.value("cwd").toString();
    if (cwd.isEmpty() && workspace_)
        cwd = workspace_();
    if (!QFileInfo(cwd).isDir() || cwd.contains(QChar('\0'))) {
        reject("Debug terminal working directory is invalid.");
        return;
    }
    const auto env = arguments.value("env");
    if (!env.isUndefined() && !env.isNull() && !env.isObject()) {
        reject("Invalid debug terminal environment.");
        return;
    }
    auto environment = terminal::refreshedProcessEnvironment();
    const auto additions = env.toObject();
    for (auto item = additions.begin(); item != additions.end(); ++item) {
        if (item.key().isEmpty() || item.key().contains(QChar('\0')) || item.key().contains('=')) {
            reject("Invalid debug terminal environment name.");
            return;
        }
        if (item.value().isNull())
            environment.remove(item.key());
        else if (item.value().isString() && !item.value().toString().contains(QChar('\0')))
            environment.insert(item.key(), item.value().toString());
        else {
            reject("Invalid debug terminal environment value.");
            return;
        }
    }
    const auto id = terminal_->runDebugCommand(arguments.value("title").toString(), command,
                                               environment, cwd, interpret.toBool());
    if (!id.isValid()) {
        reject("Could not create the debug terminal.");
        return;
    }
    pending_.insert(id.value(), request);
    leased_.append(id);
    emit terminalCreated();
}
} // namespace litecode::ui
