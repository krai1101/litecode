#pragma once

#include "core/OperationResult.h"

#include <QMetaType>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVector>

namespace litecode::terminal {

inline constexpr qsizetype terminalInputQueueLimit = 256 * 1024;
inline constexpr qsizetype terminalOutputQueueLimit = 1024 * 1024;
inline constexpr qsizetype terminalOutputDrainSlice = 64 * 1024;

enum class TerminalState {
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
};

struct TerminalProfile final {
    QString id;
    QString displayName;
    QString executable;
    QStringList arguments;
    QProcessEnvironment environment;

    [[nodiscard]] bool isValid() const noexcept {
        return !id.isEmpty() && !displayName.isEmpty() && !executable.isEmpty();
    }
};

struct TerminalExit final {
    core::OperationId operationId;
    core::CompletionKind completion{core::CompletionKind::Failed};
    int exitCode{-1};
    bool crashed{false};
    QString safeDiagnostic;
};

[[nodiscard]] QVector<TerminalProfile> availableTerminalProfiles();
[[nodiscard]] TerminalProfile defaultTerminalProfile();
[[nodiscard]] QProcessEnvironment refreshedProcessEnvironment();

} // namespace litecode::terminal

Q_DECLARE_METATYPE(litecode::terminal::TerminalState)
Q_DECLARE_METATYPE(litecode::terminal::TerminalProfile)
Q_DECLARE_METATYPE(litecode::terminal::TerminalExit)
