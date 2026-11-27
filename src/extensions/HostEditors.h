#pragma once
#include "extensions/EditorApi.h"
#include "extensions/EditorStateWire.h"
#include "extensions/HostDocuments.h"
#include "extensions/HostWorkspaceRequests.h"
#include <QPointer>

namespace litecode::extensions {
class HostEditors final : public QObject {
  public:
    HostEditors(HostDocuments& documents, HostWorkspaceRequests& requests);
    ~HostEditors() override;
    std::shared_ptr<TextEditor> activeTextEditor() const;
    QVector<std::shared_ptr<TextEditor>> visibleTextEditors() const;
    Event<std::shared_ptr<TextEditor>> onDidChangeActiveTextEditor() const { return activeChanged_.event(); }
    Event<QVector<std::shared_ptr<TextEditor>>> onDidChangeVisibleTextEditors() const { return visibleChanged_.event(); }
    Event<TextEditorSelectionChangeEvent> onDidChangeTextEditorSelection() const { return selectionChanged_.event(); }
    bool accept(const WindowEditorState& state, bool initial = false);
    void shutdown();
  private:
    class Editor;
    QPointer<HostDocuments> documents_;
    QPointer<HostWorkspaceRequests> requests_;
    mutable QMutex mutex_;
    QHash<QString, std::shared_ptr<Editor>> editors_;
    QStringList visibleIds_;
    std::optional<QString> activeId_;
    bool stopped_{};
    EventEmitter<std::shared_ptr<TextEditor>> activeChanged_;
    EventEmitter<QVector<std::shared_ptr<TextEditor>>> visibleChanged_;
    EventEmitter<TextEditorSelectionChangeEvent> selectionChanged_;
};
} // namespace litecode::extensions
