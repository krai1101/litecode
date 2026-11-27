#pragma once
#include <QString>
#include <optional>

namespace litecode::extensions {
struct MessageItem {
    QString title;
    std::optional<bool> isCloseAffordance;
};
struct MessageOptions {
    std::optional<bool> modal;
    std::optional<QString> detail;
};
} // namespace litecode::extensions
