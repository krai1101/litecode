#pragma once
#include "extensions/PrimitivesApi.h"
#include <QFuture>
#include <QString>
#include <optional>
#include <variant>

namespace litecode::extensions {
template <typename T> class Progress {
  public:
    virtual ~Progress() = default;
    virtual void report(const T& value) = 0;
};
enum class ProgressLocation { SourceControl = 1, Window = 10, Notification = 15 };
struct ProgressOptions {
    struct ViewLocation {
        QString viewId;
    };
    std::variant<ProgressLocation, ViewLocation> location;
    std::optional<QString> title;
    std::optional<bool> cancellable;
};
// Native name for the anonymous { message?: string; increment?: number }
// payload in window.withProgress. Increments are deltas, not absolute values.
struct ProgressReport {
    std::optional<QString> message;
    std::optional<double> increment;
};
namespace detail {
template <typename T> struct ProgressFutureValue;
template <typename T> struct ProgressFutureValue<QFuture<T>> {
    using Type = T;
};
// Native implementation hook, not a public extension operation.
struct ProgressSession {
    std::shared_ptr<Progress<ProgressReport>> progress;
    CancellationToken token;
    std::function<void()> finish;
};
} // namespace detail
} // namespace litecode::extensions
