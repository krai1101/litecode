#pragma once

#include <QByteArray>
#include <QString>
#include <Qt>

namespace litecode::core {

[[nodiscard]] Qt::CaseSensitivity fileSystemCaseSensitivity(const QString& path);
[[nodiscard]] QString normalizedFileSystemPath(const QString& path);
[[nodiscard]] QByteArray fileSystemEntryIdentity(const QString& path);
[[nodiscard]] bool pathsReferToSameEntry(const QString& left, const QString& right);

} // namespace litecode::core
