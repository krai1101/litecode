#pragma once

#include <memory>

class QObject;

namespace litecode::terminal {
class ITerminalBackend;

std::unique_ptr<ITerminalBackend> createTerminalBackend(QObject* parent = nullptr);

} // namespace litecode::terminal
