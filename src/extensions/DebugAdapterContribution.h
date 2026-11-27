#pragma once
#include "extensions/DebugApi.h"
namespace litecode::extensions {
enum class DebugAdapterPlatform { Windows, MacOS, Linux };
DebugAdapterPlatform currentDebugAdapterPlatform();
// Private manifest interpretation, distinct from target launch configuration.
QString validateDebugAdapterContribution(const QJsonObject& contribution);
std::optional<DebugAdapterExecutable>
debugAdapterExecutableFromContribution(const QJsonObject& contribution,
                                       const QString& extensionDirectory,
                                       DebugAdapterPlatform platform, bool windowsWow64 = false);
} // namespace litecode::extensions
