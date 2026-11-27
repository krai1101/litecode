#pragma once
#include <QString>
#include <QJsonArray>
#include <optional>
namespace litecode::extensions {
struct Command final {
    QString title;
    QString command;
    std::optional<QString> tooltip;
    QJsonArray arguments;
};
}
