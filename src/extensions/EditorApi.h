#pragma once
#include "extensions/DocumentApi.h"
#include "extensions/EditorEditApi.h"
#include <functional>
#include <memory>

namespace litecode::extensions {
// Selected stable editor members needed by chat integrations. This does not
// declare general options, decorations, snippets or full editor API parity.
struct TextEditorEditOptions final {
    bool undoStopBefore{true}, undoStopAfter{true};
};
class TextEditor {
  public:
    using EditCallback = std::function<void(std::shared_ptr<TextEditorEdit>)>;
    virtual ~TextEditor() = default;
    [[nodiscard]] virtual std::shared_ptr<TextDocument> document() const = 0;
    [[nodiscard]] virtual Selection selection() const = 0;
    [[nodiscard]] virtual QVector<Selection> selections() const = 0;
    virtual QFuture<bool> edit(const EditCallback& callback,
                               TextEditorEditOptions options = {}) = 0;
};
enum class TextEditorSelectionChangeKind { Keyboard = 1, Mouse = 2, Command = 3 };
struct TextEditorSelectionChangeEvent final {
    std::shared_ptr<TextEditor> textEditor;
    QVector<Selection> selections;
    std::optional<TextEditorSelectionChangeKind> kind;
};
} // namespace litecode::extensions
