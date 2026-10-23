#pragma once

#include <QMetaType>
#include <QProcess>
#include <QString>
#include <QtTypes>

namespace litecode::core {

class OperationId final {
  public:
    constexpr OperationId() noexcept = default;
    explicit constexpr OperationId(quint64 value) noexcept : value_(value) {}

    [[nodiscard]] constexpr quint64 value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(OperationId, OperationId) noexcept = default;

  private:
    quint64 value_{};
};

// Instantiate this as state owned by the service issuing operations. Deliberately not a singleton.
class OperationIdGenerator final {
  public:
    [[nodiscard]] OperationId next() noexcept;

  private:
    quint64 next_{1};
};

enum class CompletionKind {
    Succeeded,
    Failed,
    Cancelled,
    TimedOut,
};

// An immutable published result. Services build one value at their single completion point.
class ProcessResult final {
  public:
    ProcessResult(OperationId operationId, CompletionKind completion, int exitCode,
                  QProcess::ExitStatus exitStatus, QString safeDiagnostic = {});

    [[nodiscard]] OperationId operationId() const noexcept { return operationId_; }
    [[nodiscard]] CompletionKind completion() const noexcept { return completion_; }
    [[nodiscard]] int exitCode() const noexcept { return exitCode_; }
    [[nodiscard]] QProcess::ExitStatus exitStatus() const noexcept { return exitStatus_; }
    [[nodiscard]] const QString& safeDiagnostic() const noexcept { return safeDiagnostic_; }
    [[nodiscard]] bool succeeded() const noexcept;

  private:
    OperationId operationId_;
    CompletionKind completion_;
    int exitCode_;
    QProcess::ExitStatus exitStatus_;
    QString safeDiagnostic_;
};

} // namespace litecode::core

Q_DECLARE_METATYPE(litecode::core::OperationId)
Q_DECLARE_METATYPE(litecode::core::CompletionKind)
Q_DECLARE_METATYPE(litecode::core::ProcessResult)
