#pragma once

#include "terminal/ITerminalBackend.h"
#include "terminal/TerminalInputQueue.h"

#include <QByteArray>

class QSocketNotifier;
class QTimer;

namespace litecode::terminal {

class UnixPtyBackend final : public ITerminalBackend {
    Q_OBJECT

  public:
    explicit UnixPtyBackend(QObject* parent = nullptr);
    ~UnixPtyBackend() override;

    void start(core::OperationId operationId, const TerminalProfile& profile,
               const QString& workingDirectory) override;
    [[nodiscard]] bool enqueueInput(const QByteArray& bytes) override;
    void resizeTerminal(const QSize& characters) override;
    void requestStop() override;
    [[nodiscard]] TerminalState state() const override;

  private:
    void setState(TerminalState state);
    void readAvailable();
    void flushInput();
    void drainOutput();
    void pollProcess();
    void closeMaster();
    void finish(int status, bool hasStatus, core::CompletionKind completion,
                const QString& diagnostic = {});

    core::OperationId operationId_;
    QSize terminalSize_{80, 24};
    TerminalState state_{TerminalState::Stopped};
    int masterFd_{-1};
    qint64 processId_{-1};
    bool stopRequested_{false};
    bool completionEmitted_{false};
    int stopPolls_{0};
    int foregroundPollTicks_{0};
    QString foregroundProcessName_;
    QSocketNotifier* readNotifier_{};
    QSocketNotifier* writeNotifier_{};
    QTimer* processTimer_{};
    TerminalInputQueue inputQueue_;
    QByteArray pendingOutput_;
    bool outputDrainScheduled_{false};
};

} // namespace litecode::terminal
