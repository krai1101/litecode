#pragma once

#include "ui/components/ModalDialogQueue.h"
#include "ui/extensions/WorkspaceEditReview.h"
#include <QDialog>
#include <QFuture>
#include <QHash>
#include <QPromise>
#include <QVector>
#include <memory>

namespace litecode::ui {
class WorkspaceEditReviewService final : public QObject {
  public:
    explicit WorkspaceEditReviewService(QWidget* window, QObject* parent = nullptr);
    ~WorkspaceEditReviewService() override;
    QFuture<bool> review(quint64 id, const QString& owner,
                         QVector<WorkspaceEditReviewDocument> documents);
    void cancel(quint64 id);

  private:
    struct Operation {
        std::shared_ptr<QPromise<bool>> promise;
        QPointer<QDialog> dialog;
    };
    void finish(quint64 id, bool accepted);
    QPointer<QWidget> window_;
    QPointer<ModalDialogQueue> queue_;
    QHash<quint64, Operation> operations_;
};
} // namespace litecode::ui
