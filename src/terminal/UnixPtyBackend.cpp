#include "terminal/UnixPtyBackend.h"

#include <QFile>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(Q_OS_MACOS)
#include <libproc.h>
#include <util.h>
#else
#include <pty.h>
#endif

namespace litecode::terminal {
namespace {

constexpr int processPollIntervalMs = 50;
constexpr int gracefulStopPolls = 10;
constexpr int foregroundProcessPolls = 10;
constexpr qsizetype readBufferSize = 16 * 1024;

QString systemError(const char* operation) {
    return QStringLiteral("%1 failed: %2")
        .arg(QString::fromLatin1(operation), QString::fromLocal8Bit(std::strerror(errno)));
}

void signalProcessGroup(pid_t processId, int signal) {
    if (processId <= 0)
        return;
    // forkpty creates a session/process group for the child. Signalling the group prevents
    // commands spawned by the shell from surviving when a terminal is closed.
    if (::kill(-processId, signal) != 0 && errno == ESRCH)
        (void)::kill(processId, signal);
}

void reapKilledChild(pid_t child) {
    signalProcessGroup(child, SIGKILL);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
}

void reapKilledChildAsync(pid_t child) {
    QThread* reaper = QThread::create([child] { reapKilledChild(child); });
    QObject::connect(reaper, &QThread::finished, reaper, &QObject::deleteLater);
    reaper->start();
}

QString processName(pid_t processId) {
#if defined(Q_OS_MACOS)
    char name[PROC_PIDPATHINFO_MAXSIZE]{};
    if (proc_name(processId, name, sizeof(name)) <= 0)
        return {};
    return QString::fromLocal8Bit(name).trimmed();
#else
    QFile comm(QStringLiteral("/proc/%1/comm").arg(processId));
    if (!comm.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLocal8Bit(comm.readAll()).trimmed();
#endif
}

} // namespace

UnixPtyBackend::UnixPtyBackend(QObject* parent)
    : ITerminalBackend(parent), processTimer_(new QTimer(this)) {
    processTimer_->setInterval(processPollIntervalMs);
    connect(processTimer_, &QTimer::timeout, this, &UnixPtyBackend::pollProcess);
}

UnixPtyBackend::~UnixPtyBackend() {
    processTimer_->stop();
    closeMaster();
    if (processId_ > 0) {
        const pid_t child = static_cast<pid_t>(processId_);
        processId_ = -1;
        reapKilledChildAsync(child);
    }
}

void UnixPtyBackend::start(core::OperationId operationId, const TerminalProfile& profile,
                           const QString& workingDirectory) {
    if (state_ != TerminalState::Stopped || !profile.isValid()) {
        emit errorOccurred(tr("The terminal profile is invalid or a terminal is already active."));
        return;
    }

    operationId_ = operationId;
    completionEmitted_ = false;
    stopRequested_ = false;
    stopPolls_ = 0;
    foregroundPollTicks_ = foregroundProcessPolls;
    foregroundProcessName_.clear();
    inputQueue_.clear();
    pendingOutput_.clear();
    setState(TerminalState::Starting);

    winsize initialSize{};
    initialSize.ws_col = static_cast<unsigned short>(terminalSize_.width());
    initialSize.ws_row = static_cast<unsigned short>(terminalSize_.height());
    const QByteArray directory = QFile::encodeName(workingDirectory);
    const QByteArray executable = QFile::encodeName(profile.executable);
    QVector<QByteArray> encodedArguments;
    encodedArguments.reserve(profile.arguments.size() + 1);
    encodedArguments.push_back(executable);
    for (const QString& argument : profile.arguments)
        encodedArguments.push_back(QFile::encodeName(argument));
    QVector<char*> arguments;
    arguments.reserve(encodedArguments.size() + 1);
    for (QByteArray& argument : encodedArguments)
        arguments.push_back(argument.data());
    arguments.push_back(nullptr);
    QVector<QByteArray> encodedEnvironment;
    const QStringList environmentEntries = profile.environment.toStringList();
    encodedEnvironment.reserve(environmentEntries.size());
    for (const QString& entry : environmentEntries)
        encodedEnvironment.push_back(entry.toLocal8Bit());
    QVector<char*> environment;
    environment.reserve(encodedEnvironment.size() + 1);
    for (QByteArray& entry : encodedEnvironment)
        environment.push_back(entry.data());
    environment.push_back(nullptr);

    int master = -1;
    const pid_t child = forkpty(&master, nullptr, nullptr, &initialSize);
    if (child < 0) {
        const QString diagnostic = systemError("forkpty");
        setState(TerminalState::Failed);
        finish(0, false, core::CompletionKind::Failed, diagnostic);
        return;
    }

    if (child == 0) {
        if (!directory.isEmpty() && ::chdir(directory.constData()) != 0)
            _exit(126);
        ::execve(executable.constData(), arguments.data(), environment.data());
        _exit(127);
    }

    const int flags = ::fcntl(master, F_GETFL, 0);
    if (flags < 0 || ::fcntl(master, F_SETFL, flags | O_NONBLOCK) != 0) {
        const QString diagnostic = systemError("fcntl(O_NONBLOCK)");
        ::close(master);
        reapKilledChildAsync(child);
        setState(TerminalState::Failed);
        finish(0, false, core::CompletionKind::Failed, diagnostic);
        return;
    }

    masterFd_ = master;
    processId_ = child;
    readNotifier_ = new QSocketNotifier(masterFd_, QSocketNotifier::Read, this);
    writeNotifier_ = new QSocketNotifier(masterFd_, QSocketNotifier::Write, this);
    writeNotifier_->setEnabled(false);
    connect(readNotifier_, &QSocketNotifier::activated, this, &UnixPtyBackend::readAvailable);
    connect(writeNotifier_, &QSocketNotifier::activated, this, &UnixPtyBackend::flushInput);
    processTimer_->start();
    setState(TerminalState::Running);
    emit started(operationId_);
}

bool UnixPtyBackend::enqueueInput(const QByteArray& bytes) {
    if (bytes.isEmpty())
        return true;
    if (state_ != TerminalState::Running || masterFd_ < 0) {
        emit inputRejected(operationId_, tr("The terminal is not accepting input."));
        return false;
    }
    if (!inputQueue_.enqueue(bytes)) {
        emit inputRejected(operationId_, tr("Terminal input queue is full."));
        return false;
    }
    flushInput();
    return true;
}

void UnixPtyBackend::resizeTerminal(const QSize& characters) {
    if (characters.width() <= 0 || characters.height() <= 0)
        return;
    terminalSize_ =
        QSize(std::clamp(characters.width(), 1, 32767), std::clamp(characters.height(), 1, 32767));
    if (masterFd_ < 0)
        return;
    winsize size{};
    size.ws_col = static_cast<unsigned short>(terminalSize_.width());
    size.ws_row = static_cast<unsigned short>(terminalSize_.height());
    if (::ioctl(masterFd_, TIOCSWINSZ, &size) != 0)
        emit errorOccurred(systemError("ioctl(TIOCSWINSZ)"));
}

void UnixPtyBackend::requestStop() {
    if (processId_ <= 0 || state_ == TerminalState::Stopping || state_ == TerminalState::Stopped)
        return;
    stopRequested_ = true;
    stopPolls_ = 0;
    setState(TerminalState::Stopping);
    if (writeNotifier_)
        writeNotifier_->setEnabled(false);
    inputQueue_.clear();
    signalProcessGroup(static_cast<pid_t>(processId_), SIGHUP);
    processTimer_->start();
}

TerminalState UnixPtyBackend::state() const { return state_; }

void UnixPtyBackend::setState(TerminalState state) {
    if (state_ == state)
        return;
    state_ = state;
    emit stateChanged(operationId_, state_);
}

void UnixPtyBackend::readAvailable() {
    if (masterFd_ < 0)
        return;
    QByteArray chunk(readBufferSize, Qt::Uninitialized);
    qsizetype readThisTurn = 0;
    while (readThisTurn < terminalOutputDrainSlice) {
        const qsizetype available = terminalOutputQueueLimit - pendingOutput_.size();
        if (available <= 0) {
            readNotifier_->setEnabled(false);
            break;
        }
        const ssize_t count =
            ::read(masterFd_, chunk.data(), static_cast<size_t>(std::min(chunk.size(), available)));
        if (count > 0) {
            pendingOutput_.append(chunk.constData(), static_cast<qsizetype>(count));
            readThisTurn += count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        closeMaster();
        break;
    }
    if (!outputDrainScheduled_) {
        outputDrainScheduled_ = true;
        QTimer::singleShot(0, this, &UnixPtyBackend::drainOutput);
    }
}

void UnixPtyBackend::flushInput() {
    if (masterFd_ < 0 || state_ != TerminalState::Running)
        return;
    while (!inputQueue_.isEmpty()) {
        const QByteArray bytes = inputQueue_.nextSlice(readBufferSize);
        const ssize_t written =
            ::write(masterFd_, bytes.constData(), static_cast<size_t>(bytes.size()));
        if (written > 0) {
            inputQueue_.consume(static_cast<qsizetype>(written));
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            writeNotifier_->setEnabled(true);
            return;
        }
        const QString diagnostic = systemError("write");
        emit errorOccurred(diagnostic);
        requestStop();
        return;
    }
    if (writeNotifier_)
        writeNotifier_->setEnabled(false);
}

void UnixPtyBackend::drainOutput() {
    outputDrainScheduled_ = false;
    if (pendingOutput_.isEmpty())
        return;
    const qsizetype count = std::min(pendingOutput_.size(), terminalOutputDrainSlice);
    emit outputReady(pendingOutput_.first(count));
    pendingOutput_.remove(0, count);
    if (!pendingOutput_.isEmpty()) {
        outputDrainScheduled_ = true;
        QTimer::singleShot(0, this, &UnixPtyBackend::drainOutput);
    }
    if (readNotifier_ && !readNotifier_->isEnabled() &&
        pendingOutput_.size() < terminalOutputQueueLimit)
        readNotifier_->setEnabled(true);
}

void UnixPtyBackend::pollProcess() {
    if (processId_ <= 0) {
        processTimer_->stop();
        return;
    }
    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(processId_), &status, WNOHANG);
    if (result == 0) {
        if (++foregroundPollTicks_ >= foregroundProcessPolls) {
            foregroundPollTicks_ = 0;
            const pid_t foregroundGroup = ::tcgetpgrp(masterFd_);
            const QString name = foregroundGroup > 0 ? processName(foregroundGroup) : QString{};
            if (!name.isEmpty() && name != foregroundProcessName_) {
                foregroundProcessName_ = name;
                emit foregroundProcessChanged(operationId_, name);
            }
        }
        if (stopRequested_ && ++stopPolls_ == gracefulStopPolls)
            signalProcessGroup(static_cast<pid_t>(processId_), SIGTERM);
        else if (stopRequested_ && stopPolls_ == gracefulStopPolls * 2)
            signalProcessGroup(static_cast<pid_t>(processId_), SIGKILL);
        return;
    }
    if (result < 0 && errno == EINTR)
        return;
    if (result < 0) {
        finish(0, false, core::CompletionKind::Failed, systemError("waitpid"));
        return;
    }
    finish(status, true,
           stopRequested_ ? core::CompletionKind::Cancelled : core::CompletionKind::Succeeded);
}

void UnixPtyBackend::closeMaster() {
    if (readNotifier_) {
        readNotifier_->setEnabled(false);
        delete readNotifier_;
        readNotifier_ = nullptr;
    }
    if (writeNotifier_) {
        writeNotifier_->setEnabled(false);
        delete writeNotifier_;
        writeNotifier_ = nullptr;
    }
    if (masterFd_ >= 0) {
        ::close(masterFd_);
        masterFd_ = -1;
    }
}

void UnixPtyBackend::finish(int status, bool hasStatus, core::CompletionKind completion,
                            const QString& diagnostic) {
    if (completionEmitted_)
        return;
    completionEmitted_ = true;
    processTimer_->stop();
    // The process may exit before the read notifier runs. Consume bytes already in the PTY
    // before closing it, otherwise a short-lived command can lose its final output.
    if (masterFd_ >= 0) {
        QByteArray chunk(readBufferSize, Qt::Uninitialized);
        for (;;) {
            const qsizetype available = terminalOutputQueueLimit - pendingOutput_.size();
            if (available <= 0) {
                drainOutput();
                continue;
            }
            const ssize_t count = ::read(masterFd_, chunk.data(),
                                         static_cast<size_t>(std::min(chunk.size(), available)));
            if (count > 0) {
                pendingOutput_.append(chunk.constData(), static_cast<qsizetype>(count));
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            break;
        }
    }
    closeMaster();
    processId_ = -1;
    while (!pendingOutput_.isEmpty())
        drainOutput();
    TerminalExit result;
    result.operationId = operationId_;
    result.completion = completion;
    result.exitCode = hasStatus && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.crashed = hasStatus && WIFSIGNALED(status);
    result.safeDiagnostic = diagnostic;
    setState(TerminalState::Stopped);
    if (!diagnostic.isEmpty())
        emit errorOccurred(diagnostic);
    emit completed(result);
}

} // namespace litecode::terminal
