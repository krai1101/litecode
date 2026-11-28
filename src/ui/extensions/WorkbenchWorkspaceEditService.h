#pragma once
#include "extensions/WorkspaceEditWire.h"
#include "ui/EditorArea.h"
#include "ui/extensions/WorkbenchDocumentOpenService.h"
#include <QPromise>
namespace litecode::ui {
class WorkspaceEditReviewService;
class WorkbenchWorkspaceEditService final : public QObject {
  public:
    WorkbenchWorkspaceEditService(EditorArea* editors, WorkbenchDocumentOpenService* opens,
                                  QObject* parent = nullptr);
    ~WorkbenchWorkspaceEditService() override;
    QFuture<bool> apply(const QString& owner, const extensions::WorkspaceEditRequest& request);
    void removeOwner(const QString& owner);
    void setReviewService(WorkspaceEditReviewService* reviews);

  private:
    struct Operation {
        QString owner, referenceOwner;
        QVector<QPair<QUrl, QVector<extensions::TextEdit>>> groups;
        QHash<QString, qint64> versions;
        QVector<DocumentEditPlan> plans;
        qsizetype index{};
        std::shared_ptr<QPromise<bool>> promise;
        QHash<QString, QStringList> annotations;
        bool needsReview{};
    };
    void next(quint64 id);
    void finish(quint64 id, bool applied);
    void review(quint64 id);
    QPointer<EditorArea> editors_;
    QPointer<WorkbenchDocumentOpenService> opens_;
    QPointer<WorkspaceEditReviewService> reviews_;
    QHash<quint64, std::shared_ptr<Operation>> operations_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::ui
