#include "debug/TaskRunner.h"
#include <QDir>

namespace litecode::debug {
namespace {
QString shellArgument(const QString& value) {
#ifdef Q_OS_WIN
    QString escaped = value;
    escaped.replace('"', "\\\"");
    return '"' + escaped + '"';
#else
    QString escaped = value;
    escaped.replace(QChar(39), "'\\''");
    return '\'' + escaped + '\'';
#endif
}
} // namespace
TaskRunner::TaskRunner(QObject* parent) : QObject(parent), process_(this) {
    connect(&process_, &QProcess::readyReadStandardOutput, this,
            [this] { emit outputReceived(QString::fromUtf8(process_.readAllStandardOutput())); });
    connect(&process_, &QProcess::readyReadStandardError, this,
            [this] { emit outputReceived(QString::fromUtf8(process_.readAllStandardError())); });
    connect(&process_, &QProcess::started, this, [this] {
        if (cancelling_)
            process_.kill();
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (active_ && error == QProcess::FailedToStart)
            finish(false, cancelling_ ? tr("Task cancelled.")
                                      : tr("Task '%1' could not start: %2")
                                            .arg(currentLabel_, process_.errorString()));
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!active_)
            return;
        if (cancelling_) {
            finish(false, tr("Task cancelled."));
            return;
        }
        if (status != QProcess::NormalExit || code != 0) {
            finish(false, tr("Task '%1' failed with exit code %2.").arg(currentLabel_).arg(code));
            return;
        }
        runNext();
    });
}
bool TaskRunner::start(const QString& workspace, const QString& label,
                       const VariableContext& context) {
    if (active_ || workspace.isEmpty() || label.isEmpty())
        return false;
    QString error;
    const auto tasks = TaskConfigurations::load(workspace, &error);
    if (error.isEmpty())
        graph_ = TaskConfigurations::dependencyGraph(tasks, label, &error);
    if (!error.isEmpty() || graph_.isEmpty()) {
        emit finished(false, error.isEmpty() ? tr("No task to run.") : error);
        return false;
    }
    context_ = context;
    nextIndex_ = 0;
    active_ = true;
    runNext();
    return active_;
}
void TaskRunner::stop() {
    if (!active_ || cancelling_)
        return;
    if (process_.state() == QProcess::NotRunning) {
        finish(false, tr("Task cancelled."));
    } else {
        cancelling_ = true;
        process_.kill();
    }
}
void TaskRunner::finish(bool success, const QString& error) {
    if (!active_)
        return;
    active_ = false;
    cancelling_ = false;
    graph_.clear();
    emit finished(success, error);
}
void TaskRunner::runNext() {
    while (active_ && nextIndex_ < graph_.size()) {
        const auto task = graph_.at(nextIndex_++);
        if (task.type == "compound")
            continue;
        QString error;
        const auto command = TaskConfigurations::resolve(task, context_, &error);
        if (!error.isEmpty() || command.command.isEmpty() || !QDir(command.cwd).exists()) {
            finish(false, error.isEmpty()
                              ? tr("Invalid command or working directory for '%1'.").arg(task.label)
                              : error);
            return;
        }
        currentLabel_ = task.label;
        process_.setWorkingDirectory(command.cwd);
        process_.setProcessEnvironment(command.environment);
        emit outputReceived(tr("> Task: %1\n").arg(task.label));
        if (command.type == "shell") {
            QString script = command.command;
            for (const auto& argument : command.arguments)
                script += ' ' + shellArgument(argument);
#ifdef Q_OS_WIN
            process_.start(QStringLiteral("cmd.exe"), {"/d", "/s", "/c", script});
#else
            process_.start(QStringLiteral("/bin/sh"), {"-c", script});
#endif
        } else {
            process_.start(command.command, command.arguments);
        }
        return;
    }
    finish(true, {});
}
} // namespace litecode::debug
