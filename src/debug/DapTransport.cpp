#include "debug/DapTransport.h"
#include "debug/DapSocketConnection.h"
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace litecode::debug {
DapTransport::DapTransport(QObject* parent) : QObject(parent), channel_(this) {
    startup_.setSingleShot(true);
    shutdown_.setSingleShot(true);
    connect(&startup_, &QTimer::timeout, this,
            [this] { abort("Debug adapter startup timed out."); });
    connect(&shutdown_, &QTimer::timeout, this, [this] {
        if (process_ && process_->state() != QProcess::NotRunning)
            process_->kill();
    });
    connect(&channel_, &DapChannel::responseReceived, this, &DapTransport::responseReceived);
    connect(&channel_, &DapChannel::eventReceived, this, &DapTransport::eventReceived);
    connect(&channel_, &DapChannel::responseValueReceived, this,
            &DapTransport::responseValueReceived);
    connect(&channel_, &DapChannel::eventValueReceived, this, &DapTransport::eventValueReceived);
    connect(&channel_, &DapChannel::messageSent, this, &DapTransport::messageSent);
    connect(&channel_, &DapChannel::messageReceived, this, &DapTransport::messageReceived);
    connect(&channel_, &DapChannel::reverseRequestReceived, this,
            &DapTransport::reverseRequestReceived);
    connect(&channel_, &DapChannel::protocolError, this, &DapTransport::abort);
    connect(&channel_, &DapChannel::writeRequested, this,
            [this](const QByteArray& frame, quint64 generation) {
                if ((!process_ && !connection_) || state_ != State::Running ||
                    generation != channel_.generation())
                    return;
                constexpr qint64 MaxQueuedBytes = 32 * 1024 * 1024;
                const auto queued =
                    process_ ? process_->bytesToWrite() : connection_->queuedBytes();
                if (queued + frame.size() > MaxQueuedBytes) {
                    abort("Debug adapter input queue exceeded its size limit.");
                    return;
                }
                if (process_ ? process_->write(frame) != frame.size() : !connection_->write(frame))
                    abort("Could not write to the debug adapter.");
            });
}
DapTransport::~DapTransport() {
    if (connection_) {
        connection_->disconnect(this);
        connection_->close();
    }
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(1000);
    }
}
void DapTransport::setState(State state) {
    if (state_ == state)
        return;
    state_ = state;
    emit stateChanged(state);
}
static bool safeArgument(const QString& value) { return !value.contains(QChar('\0')); }
bool DapTransport::start(const AdapterProcess& configuration) {
    if (state_ != State::Idle || process_ || connection_ || finishing_)
        return false;
    if (configuration.executable.isEmpty() || !safeArgument(configuration.executable) ||
        !safeArgument(configuration.workingDirectory) || configuration.startupTimeoutMs <= 0) {
        emit errorOccurred("Invalid debug adapter process configuration.");
        return false;
    }
    for (const auto& argument : configuration.arguments)
        if (!safeArgument(argument)) {
            emit errorOccurred("Invalid debug adapter argument.");
            return false;
        }
    QString executable = configuration.executable;
    if (!QFileInfo(executable).isAbsolute()) {
        // PATH lookup only; relative paths containing separators are intentionally
        // rejected so a workspace cannot silently replace a configured adapter.
        if (executable.contains('/') || executable.contains('\\')) {
            emit errorOccurred("Use an absolute path for the debug adapter.");
            return false;
        }
        // Use the environment selected for this adapter, not LiteCode's own
        // PATH. Provider executable options may select a different toolchain.
        const auto paths = configuration.environment.value(QStringLiteral("PATH"))
                               .split(QDir::listSeparator(), Qt::SkipEmptyParts);
        if (paths.isEmpty()) {
            emit errorOccurred("Debug adapter PATH is empty; use an absolute executable path.");
            return false;
        }
        executable = QStandardPaths::findExecutable(executable, paths);
    }
    if (!QFileInfo(executable).isFile() || !QFileInfo(executable).isExecutable()) {
        emit errorOccurred("Debug adapter executable was not found or is not executable.");
        return false;
    }
    if (!configuration.workingDirectory.isEmpty() &&
        !QDir(configuration.workingDirectory).exists()) {
        emit errorOccurred("Debug adapter working directory does not exist.");
        return false;
    }
    channel_.reset();
    const auto generation = channel_.generation();
    auto* process = new QProcess(this);
    process_ = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setProgram(QFileInfo(executable).absoluteFilePath());
    process->setArguments(configuration.arguments);
    process->setWorkingDirectory(configuration.workingDirectory);
    process->setProcessEnvironment(configuration.environment);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    connect(process, &QProcess::started, this, [this, process] {
        if (process != process_ || state_ != State::Starting)
            return;
        startup_.stop();
        setState(State::Running);
        if (process == process_ && state_ == State::Running)
            emit started();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, generation] {
        if (process == process_)
            drain(process, generation);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (process != process_)
            return;
        process->setReadChannel(QProcess::StandardError);
        while (process->bytesAvailable() > 0) {
            emit diagnosticOutput(process->read(65536));
            if (process != process_)
                return;
        }
        process->setReadChannel(QProcess::StandardOutput);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process != process_)
            return;
        if (error == QProcess::FailedToStart) {
            emit errorOccurred("Could not start debug adapter: " + process->errorString());
            finish(process, -1, false);
        } else if (error != QProcess::Crashed) {
            abort("Debug adapter process error: " + process->errorString());
        }
    });
    connect(process, &QProcess::finished, this,
            [this, process, generation](int exitCode, QProcess::ExitStatus status) {
                if (process != process_)
                    return;
                drain(process, generation);
                const auto diagnostics = process->readAllStandardError();
                if (!diagnostics.isEmpty())
                    emit diagnosticOutput(diagnostics.left(65536));
                finish(process, exitCode, status == QProcess::CrashExit);
            });
    setState(State::Starting);
    // stateChanged handlers may cancel startup synchronously.
    if (state_ != State::Starting) {
        finish(process, -1, false);
        return false;
    }
    startup_.start(configuration.startupTimeoutMs);
    process->start(QIODevice::ReadWrite);
    return true;
}
bool DapTransport::startServer(const AdapterServer& configuration) {
    if (configuration.port == 0 || !safeArgument(configuration.host)) {
        emit errorOccurred("Invalid debug adapter server configuration.");
        return false;
    }
    return startSocket(
        configuration.startupTimeoutMs, [&configuration](DapSocketConnection& connection) {
            connection.connectTcp(configuration.host.isEmpty() ? QStringLiteral("127.0.0.1")
                                                               : configuration.host,
                                  configuration.port);
        });
}
bool DapTransport::startNamedPipeServer(const AdapterNamedPipeServer& configuration) {
    if (configuration.path.isEmpty() || !safeArgument(configuration.path)) {
        emit errorOccurred("Invalid debug adapter pipe configuration.");
        return false;
    }
    return startSocket(configuration.startupTimeoutMs,
                       [&configuration](DapSocketConnection& connection) {
                           connection.connectLocal(configuration.path);
                       });
}
bool DapTransport::startSocket(int timeout,
                               const std::function<void(DapSocketConnection&)>& connectToServer) {
    if (state_ != State::Idle || process_ || connection_ || finishing_)
        return false;
    if (timeout <= 0) {
        emit errorOccurred("Invalid debug adapter startup timeout.");
        return false;
    }
    channel_.reset();
    const auto generation = channel_.generation();
    auto* connection = new DapSocketConnection(this);
    connection_ = connection;
    connect(connection, &DapSocketConnection::connected, this, [this, connection] {
        if (connection != connection_ || state_ != State::Starting)
            return;
        startup_.stop();
        const QPointer<DapTransport> alive(this);
        setState(State::Running);
        if (alive && connection == connection_ && state_ == State::Running)
            emit started();
    });
    connect(connection, &DapSocketConnection::bytesReceived, this,
            [this, connection, generation](const QByteArray& bytes) {
                if (connection == connection_ && state_ == State::Running)
                    channel_.receive(bytes, generation);
            });
    connect(connection, &DapSocketConnection::closed, this, [this, connection] {
        if (connection != connection_)
            return;
        const QPointer<DapTransport> alive(this);
        emit errorOccurred("Debug adapter connection closed.");
        if (alive)
            finishSocket(connection, true);
    });
    connect(connection, &DapSocketConnection::errorOccurred, this,
            [this, connection](const QString& error) {
                if (connection != connection_)
                    return;
                const QPointer<DapTransport> alive(this);
                emit errorOccurred("Debug adapter connection error: " + error);
                if (alive)
                    finishSocket(connection, true);
            });
    const QPointer<DapTransport> alive(this);
    setState(State::Starting);
    if (!alive || state_ != State::Starting || connection != connection_)
        return false;
    startup_.start(timeout);
    connectToServer(*connection);
    return true;
}
void DapTransport::finishSocket(DapSocketConnection* connection, bool unexpected) {
    if (connection != connection_ || finishing_)
        return;
    finishing_ = true;
    startup_.stop();
    shutdown_.stop();
    const QPointer<DapTransport> alive(this);
    setState(State::Stopping);
    if (!alive)
        return;
    // Disable the old stream before failure callbacks can issue another request.
    connection_ = nullptr;
    connection->disconnect(this);
    connection->close();
    connection->deleteLater();
    channel_.reset(unexpected ? "Debug adapter connection closed." : "Debug adapter disconnected.");
    if (!alive)
        return;
    setState(State::Idle);
    if (!alive)
        return;
    emit stopped(unexpected ? -1 : 0, false);
    if (alive)
        finishing_ = false;
}
void DapTransport::drain(QProcess* process, quint64 generation) {
    process->setReadChannel(QProcess::StandardOutput);
    while (process->bytesAvailable() > 0) {
        const auto bytes = process->read(65536);
        if (state_ == State::Running)
            channel_.receive(bytes, generation);
        if (process != process_)
            return;
    }
}
void DapTransport::finish(QProcess* process, int exitCode, bool crashed) {
    if (process != process_ || finishing_)
        return;
    finishing_ = true;
    // On Windows QProcess::terminate()/kill() can report CrashExit with its
    // synthetic 0xF291 exit code. An expected stop is not an adapter crash.
    const bool unexpectedlyCrashed = crashed && state_ != State::Stopping;
    startup_.stop();
    shutdown_.stop();
    // Block requests before outstanding-response failure callbacks are emitted.
    setState(State::Stopping);
    channel_.reset(unexpectedlyCrashed ? "Debug adapter crashed." : "Debug adapter exited.");
    process_ = nullptr;
    process->disconnect(this);
    process->deleteLater();
    setState(State::Idle);
    emit stopped(exitCode, unexpectedlyCrashed);
    finishing_ = false;
}
void DapTransport::stop() { stop(false); }

void DapTransport::stopAfterDisconnect() { stop(true); }

void DapTransport::stop(bool allowNaturalExit) {
    if (connection_) {
        finishSocket(connection_, false);
        return;
    }
    if (!process_ || state_ == State::Stopping)
        return;
    auto* process = process_;
    setState(State::Stopping);
    startup_.stop();
    channel_.reset();
    if (process != process_)
        return;
    if (process_->state() == QProcess::NotRunning) {
        finish(process, process->exitCode(), process->exitStatus() == QProcess::CrashExit);
        return;
    }
    if (allowNaturalExit) {
        // Acknowledging disconnect precedes process exit. Preserve the
        // adapter's own exit status while allowing it to release resources.
        process->closeWriteChannel();
        if (process == process_)
            shutdown_.start(1000);
    } else {
        process->terminate();
        if (process == process_)
            shutdown_.start(500);
    }
}
void DapTransport::abort(const QString& message) {
    auto* process = process_;
    auto* connection = connection_;
    const QPointer<DapTransport> alive(this);
    emit errorOccurred(message);
    if (alive && process == process_ && connection == connection_)
        stop();
}
int DapTransport::request(const QString& command, const QJsonObject& arguments, int timeoutMs) {
    return state_ == State::Running ? channel_.request(command, arguments, timeoutMs) : 0;
}
int DapTransport::requestValue(const QString& command, const QJsonValue& arguments, int timeoutMs) {
    return state_ == State::Running ? channel_.requestValue(command, arguments, timeoutMs) : 0;
}
bool DapTransport::respond(const QJsonObject& request, bool success, const QJsonObject& body,
                           const QString& message) {
    return state_ == State::Running && channel_.respond(request, success, body, message);
}
} // namespace litecode::debug
