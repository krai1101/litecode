#pragma once
#include <QStringList>
namespace litecode::workspace {
// Read-only discovery; does not descend symlinks or repository metadata.
QStringList discoverGitRepositories(const QString& root, int maximumDepth = 4,
                                    int maximumDirectories = 2000);
} // namespace litecode::workspace
