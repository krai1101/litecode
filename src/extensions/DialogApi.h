#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <optional>
#include <utility>

namespace litecode::extensions {
struct OpenDialogOptions {
    std::optional<QUrl> defaultUri;
    std::optional<QString> openLabel;
    std::optional<bool> canSelectFiles;
    std::optional<bool> canSelectFolders;
    std::optional<bool> canSelectMany;
    // Ordered native equivalent of the JavaScript filter dictionary. Preserve
    // insertion order: the first filter influences the initial dialog state.
    std::optional<QVector<std::pair<QString, QStringList>>> filters;
    std::optional<QString> title;
};
struct SaveDialogOptions {
    std::optional<QUrl> defaultUri;
    std::optional<QString> saveLabel;
    std::optional<QVector<std::pair<QString, QStringList>>> filters;
    std::optional<QString> title;
};
} // namespace litecode::extensions
