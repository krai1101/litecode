#pragma once
#include <QUrl>
#include <QString>
namespace litecode::extensions {
struct WorkspaceFolder final { QUrl uri; QString name; int index{}; };
} // namespace litecode::extensions
