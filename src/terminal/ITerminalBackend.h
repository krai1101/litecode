#pragma once

#include "terminal/TerminalTypes.h"

#include <QObject>
#include <QSize>

namespace litecode::terminal {

class ITerminalBackend : public QObject {
    Q_OBJECT

  public:
    using QObject::QObject;
    ~ITerminalBackend() override = default;

    virtual void start(core::OperationId operationId, const TerminalProfile& profile,
                       const QString& workingDirectory) = 0;
    [[nodiscard]] virtual bool enqueueInput(const QByteArray& bytes) = 0;
    virtual void resizeTerminal(const QSize& characters) = 0;
    virtual void requestStop() = 0;
    [[nodiscard]] virtual TerminalState state() const = 0;

  signals:
    void stateChanged(litecode::core::OperationId operationId,
                      litecode::terminal::TerminalState state);
    void started(litecode::core::OperationId operationId);
    void outputReady(const QByteArray& bytes);
    void foregroundProcessChanged(litecode::core::OperationId operationId,
                                  const QString& processName);
    void completed(const litecode::terminal::TerminalExit& result);
    void inputRejected(litecode::core::OperationId operationId, const QString& message);
    void errorOccurred(const QString& message);
};

} // namespace litecode::terminal
