#pragma once
#include <QString>
#include <QUrl>
namespace litecode::extensions {
struct WorkspaceFolder final {
    QUrl uri;
    QString name;
    int index{};
};
} // namespace litecode::extensions
