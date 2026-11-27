#pragma once
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <optional>
class QWidget;

namespace litecode::ui {
class EditorArea;
// Shared workbench saving; extension RPC remains separate from dialogs and UI.
class WorkbenchDocumentSaveService final : public QObject {
  public:
    WorkbenchDocumentSaveService(EditorArea* editors, QWidget* dialogParent, QObject* parent);
    std::optional<QUrl> saveResource(const QUrl& uri, bool saveAs, QString* error);
    bool saveAll(bool includeUntitled, QString* error);
  private:
    QPointer<EditorArea> editors_;
    QPointer<QWidget> dialogParent_;
};
} // namespace litecode::ui
