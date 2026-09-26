#pragma once

#include <QString>

namespace litecode::workspace {

// Defines the paths LiteCode deliberately excludes from workspace-wide operations.
// Search and replace must share this policy so their scopes never diverge.
[[nodiscard]] bool isWorkspacePathIgnored(const QString& relativePath);

} // namespace litecode::workspace
