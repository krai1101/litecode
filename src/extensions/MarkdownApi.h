#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>
#include <utility>
#include <variant>

namespace litecode::extensions {
// Named native representation of isTrusted's anonymous options object.
struct MarkdownStringTrustedOptions final {
    QStringList enabledCommands;
};
using MarkdownStringTrust = std::variant<std::monostate, bool, MarkdownStringTrustedOptions>;
class MarkdownString final {
  public:
    QString value;
    MarkdownStringTrust isTrusted;
    std::optional<bool> supportThemeIcons{false}, supportHtml{false};
    std::optional<QUrl> baseUri;
    MarkdownString(QString text = {}, bool icons = false)
        : value(std::move(text)), supportThemeIcons(icons) {}
    MarkdownString& appendText(const QString& text);
    MarkdownString& appendMarkdown(const QString& text);
    MarkdownString& appendCodeblock(const QString& text, const QString& language = {});
};
} // namespace litecode::extensions
