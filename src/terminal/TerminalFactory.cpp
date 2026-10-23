#include "terminal/TerminalFactory.h"

#ifdef _WIN32
#include "terminal/WindowsConPtyBackend.h"
#else
#include "terminal/UnixPtyBackend.h"
#endif

namespace litecode::terminal {

std::unique_ptr<ITerminalBackend> createTerminalBackend(QObject* parent) {
#ifdef _WIN32
    return std::make_unique<WindowsConPtyBackend>(parent);
#else
    return std::make_unique<UnixPtyBackend>(parent);
#endif
}

} // namespace litecode::terminal
