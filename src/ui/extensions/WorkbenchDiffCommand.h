#pragma once
#include <QFuture>
#include <QJsonArray>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
namespace litecode::ui {
class EditorArea;
class WorkbenchDocumentOpenService;
// Internal adapter for commands.executeCommand("vscode.diff", left, right, title, options).
class WorkbenchDiffCommand final : public QObject {
  public:
    WorkbenchDiffCommand(EditorArea* editors, WorkbenchDocumentOpenService* documents,
                         QObject* parent);
    QFuture<QJsonValue> execute(const QString& owner, const QJsonArray& arguments);

  private:
    QPointer<EditorArea> editors_;
    QPointer<WorkbenchDocumentOpenService> documents_;
};
} // namespace litecode::ui
