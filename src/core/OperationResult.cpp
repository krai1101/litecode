#include "core/OperationResult.h"

#include <limits>
#include <utility>

namespace litecode::core {

OperationId OperationIdGenerator::next() noexcept {
    if (next_ == 0) {
        return {};
    }

    const OperationId result(next_);
    if (next_ == std::numeric_limits<quint64>::max()) {
        next_ = 0;
    } else {
        ++next_;
    }
    return result;
}

ProcessResult::ProcessResult(OperationId operationId, CompletionKind completion, int exitCode,
                             QProcess::ExitStatus exitStatus, QString safeDiagnostic)
    : operationId_(operationId), completion_(completion), exitCode_(exitCode),
      exitStatus_(exitStatus), safeDiagnostic_(std::move(safeDiagnostic)) {}

bool ProcessResult::succeeded() const noexcept {
    return completion_ == CompletionKind::Succeeded && exitStatus_ == QProcess::NormalExit &&
           exitCode_ == 0;
}

} // namespace litecode::core
