#pragma once
#include "extensions/WorkspaceFolderApi.h"
#include <QJsonObject>
#include <memory>
#include <optional>
#include <variant>

namespace litecode::extensions {
class DebugSession;
class TestRun;
enum class DebugConsoleMode { Separate = 0, MergeWithParent = 1 };
struct DebugSessionOptions final {
    std::shared_ptr<DebugSession> parentSession;
    std::optional<bool> lifecycleManagedByParent;
    std::optional<DebugConsoleMode> consoleMode;
    std::optional<bool> noDebug;
    std::optional<bool> compact;
    std::optional<bool> suppressSaveBeforeStart;
    std::optional<bool> suppressDebugToolbar;
    std::optional<bool> suppressDebugStatusbar;
    std::optional<bool> suppressDebugView;
    std::shared_ptr<TestRun> testRun;
};
} // namespace litecode::extensions
