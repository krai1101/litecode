#include "workspace/WorkspaceFileScope.h"

#include <QDir>

namespace litecode::workspace {
namespace {

bool containsDirectory(const QString& normalizedPath, const QString& directoryName) {
    return normalizedPath.compare(directoryName, Qt::CaseInsensitive) == 0 ||
           normalizedPath.startsWith(directoryName + QLatin1Char('/'), Qt::CaseInsensitive) ||
           normalizedPath.contains(QLatin1Char('/') + directoryName + QLatin1Char('/'),
                                   Qt::CaseInsensitive);
}

} // namespace

bool isWorkspacePathIgnored(const QString& relativePath) {
    QString normalizedPath = QDir::fromNativeSeparators(relativePath);
    normalizedPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return containsDirectory(normalizedPath, QStringLiteral(".git")) ||
           containsDirectory(normalizedPath, QStringLiteral("build")) ||
           containsDirectory(normalizedPath, QStringLiteral("node_modules"));
}

} // namespace litecode::workspace
