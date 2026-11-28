#pragma once

#include <QLocalSocket>

namespace litecode::extensions::wire {

inline void drainSocketWrites(QLocalSocket& socket) {
#ifdef Q_OS_MACOS
    // CoreFoundation rearms socket notifications on the next run-loop pass.
    // Drain the writable buffer after each notification, rather than leaving a
    // multi-frame payload to advance one queued block per notification.
    QObject::connect(
        &socket, &QLocalSocket::bytesWritten, &socket,
        [&socket] {
            if (socket.bytesToWrite() > 0)
                socket.flush();
        },
        Qt::QueuedConnection);
#else
    Q_UNUSED(socket);
#endif
}

} // namespace litecode::extensions::wire
