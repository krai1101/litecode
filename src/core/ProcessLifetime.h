#pragma once

class QProcess;

namespace litecode::core {

// Transfers a process from a service that is being destroyed to the application lifetime, requests
// graceful termination, and escalates to kill after a fixed deadline. This never detaches the
// QProcess from an owning QObject and never blocks or pumps a nested event loop.
void retireProcess(QProcess*& process);

} // namespace litecode::core
