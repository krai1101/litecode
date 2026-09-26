#pragma once

#include "terminal/ITerminalBackend.h"

#include <atomic>
#include <memory>

class QTimer;

namespace litecode::terminal {

enum class WindowsConPtyFailurePoint {
    None,
    InputPipe,
    OutputPipe,
    PseudoConsole,
    AttributeAllocation,
    AttributeInitialization,
    AttributeUpdate,
    ProcessCreation,
    JobAssignment,
    ReaderCreation,
    WriterCreation,
};

// Test-visible ownership ledger. It counts resources owned by LiteCode's RAII wrappers, not
// handles retained internally by the operating-system ConPTY implementation.
struct WindowsConPtyResourceAudit final {
    std::atomic_int handles{0};
    std::atomic_int pseudoconsoles{0};
    std::atomic_int attributeLists{0};

    [[nodiscard]] bool allReleased() const noexcept {
        return handles.load() == 0 && pseudoconsoles.load() == 0 && attributeLists.load() == 0;
    }
};

class WindowsConPtyBackend final : public ITerminalBackend {
    Q_OBJECT

  public:
    struct RunState;

    explicit WindowsConPtyBackend(
        QObject* parent = nullptr,
        WindowsConPtyFailurePoint failurePoint = WindowsConPtyFailurePoint::None,
        std::shared_ptr<WindowsConPtyResourceAudit> resourceAudit = {});
    ~WindowsConPtyBackend() override;

    void start(core::OperationId operationId, const TerminalProfile& profile,
               const QString& workingDirectory) override;
    [[nodiscard]] bool enqueueInput(const QByteArray& bytes) override;
    void resizeTerminal(const QSize& characters) override;
    void requestStop() override;
    [[nodiscard]] TerminalState state() const override;

  private:
    void pollRun();
    void setState(TerminalState state);

    std::shared_ptr<RunState> run_;
    QTimer* pollTimer_{};
    QSize terminalSize_{120, 32};
    TerminalState state_{TerminalState::Stopped};
    WindowsConPtyFailurePoint failurePoint_{WindowsConPtyFailurePoint::None};
    std::shared_ptr<WindowsConPtyResourceAudit> resourceAudit_;
};

} // namespace litecode::terminal
