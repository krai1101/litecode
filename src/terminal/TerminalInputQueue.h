#pragma once

#include "terminal/TerminalTypes.h"

#include <QByteArray>
#include <QQueue>

namespace litecode::terminal {

// A bounded FIFO for terminal input. Backends consume only bytes confirmed written by the OS;
// a partial write or temporary would-block result therefore leaves the unwritten suffix intact.
class TerminalInputQueue final {
  public:
    explicit TerminalInputQueue(qsizetype capacity = terminalInputQueueLimit);

    [[nodiscard]] bool enqueue(const QByteArray& bytes);
    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] qsizetype queuedBytes() const noexcept;
    [[nodiscard]] QByteArray nextSlice(qsizetype maximumBytes) const;
    void consume(qsizetype byteCount);
    void clear();

  private:
    QQueue<QByteArray> chunks_;
    qsizetype firstOffset_{0};
    qsizetype queuedBytes_{0};
    qsizetype capacity_;
};

} // namespace litecode::terminal
