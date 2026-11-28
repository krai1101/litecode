#pragma once
#include "extensions/EditorEditApi.h"
#include "extensions/HostTextDocument.h"
#include "extensions/TextEditorEditData.h"
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace litecode::extensions {
class HostTextEditorEdit final : public TextEditorEdit {
  public:
    using Callback = std::function<void(std::shared_ptr<TextEditorEdit>)>;
    // The shared facade may be retained by an extension, but its mutations are
    // valid only on the callback thread and only until that callback returns.
    // No document mutation or RPC is performed when collection fails.
    static TextEditorEditData collect(std::shared_ptr<HostTextDocument> document,
                                      const Callback& callback, bool undoStopBefore = true,
                                      bool undoStopAfter = true);
    void replace(const Position& location, const QString& value) override;
    void replace(const Range& location, const QString& value) override;
    void insert(const Position& location, const QString& value) override;
    void delete_(const Range& location) override;
    void setEndOfLine(EndOfLine endOfLine) override;

  private:
    HostTextEditorEdit(std::shared_ptr<HostTextDocument> document, bool before, bool after);
    void requireActive() const;
    void push(const Range& range, std::optional<QString> text, bool forceMoveMarkers);
    TextEditorEditData finish();
    void abandon() noexcept;
    std::mutex mutex_;
    std::shared_ptr<HostTextDocument> document_;
    TextEditorEditData data_;
    const std::thread::id callbackThread_;
    bool active_{true};
};
} // namespace litecode::extensions
