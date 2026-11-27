#pragma once
#include <QString>
#include <optional>

namespace litecode::extensions {
// Named native equivalents of the anonymous options in vscode.workspace.
struct TextDocumentOpenOptions final {
    std::optional<QString> encoding;
};
struct UntitledTextDocumentOptions final {
    std::optional<QString> language;
    std::optional<QString> content;
    std::optional<QString> encoding;
};
} // namespace litecode::extensions
