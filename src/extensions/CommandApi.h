#pragma once
#include <QJsonArray>
#include <QString>
#include <optional>
namespace litecode::extensions {
struct Command final {
    QString title;
    QString command;
    std::optional<QString> tooltip;
    QJsonArray arguments;
};
} // namespace litecode::extensions
