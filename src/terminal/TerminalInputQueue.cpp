#include "terminal/TerminalInputQueue.h"

#include <QtGlobal>

#include <algorithm>

namespace litecode::terminal {

TerminalInputQueue::TerminalInputQueue(qsizetype capacity)
    : capacity_(std::max<qsizetype>(0, capacity)) {}

bool TerminalInputQueue::enqueue(const QByteArray& bytes) {
    if (bytes.isEmpty())
        return true;
    if (bytes.size() > capacity_ - queuedBytes_)
        return false;
    chunks_.enqueue(bytes);
    queuedBytes_ += bytes.size();
    return true;
}

bool TerminalInputQueue::isEmpty() const noexcept { return chunks_.isEmpty(); }

qsizetype TerminalInputQueue::queuedBytes() const noexcept { return queuedBytes_; }

QByteArray TerminalInputQueue::nextSlice(qsizetype maximumBytes) const {
    if (chunks_.isEmpty() || maximumBytes <= 0)
        return {};
    const qsizetype available = chunks_.head().size() - firstOffset_;
    return chunks_.head().sliced(firstOffset_, std::min(maximumBytes, available));
}

void TerminalInputQueue::consume(qsizetype byteCount) {
    Q_ASSERT(byteCount >= 0);
    Q_ASSERT(byteCount <= queuedBytes_);
    while (byteCount > 0 && !chunks_.isEmpty()) {
        const qsizetype available = chunks_.head().size() - firstOffset_;
        const qsizetype consumed = std::min(byteCount, available);
        firstOffset_ += consumed;
        queuedBytes_ -= consumed;
        byteCount -= consumed;
        if (firstOffset_ == chunks_.head().size()) {
            chunks_.dequeue();
            firstOffset_ = 0;
        }
    }
}

void TerminalInputQueue::clear() {
    chunks_.clear();
    firstOffset_ = 0;
    queuedBytes_ = 0;
}

} // namespace litecode::terminal
