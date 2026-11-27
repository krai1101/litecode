#include "ui/extensions/WorkspaceEditReviewService.h"
#include "ui/extensions/WorkspaceEditReviewDialog.h"

namespace litecode::ui {
WorkspaceEditReviewService::WorkspaceEditReviewService(QWidget* window, QObject* parent)
    : QObject(parent), window_(window) {
    if (window) {
        queue_ = &ModalDialogQueue::forWindow(*window);
        connect(window, &QObject::destroyed, this, [this] {
            const auto ids = operations_.keys();
            for (const auto id : ids)
                cancel(id);
        });
    }
}
WorkspaceEditReviewService::~WorkspaceEditReviewService() {
    const auto ids = operations_.keys();
    for (const auto id : ids)
        cancel(id);
}
QFuture<bool> WorkspaceEditReviewService::review(quint64 id, const QString& owner,
                                                 QVector<WorkspaceEditReviewDocument> documents) {
    auto promise = std::make_shared<QPromise<bool>>();
    promise->start();
    const auto future = promise->future();
    if (!window_ || !queue_ || !id || operations_.contains(id) || documents.isEmpty()) {
        promise->addResult(false);
        promise->finish();
        return future;
    }
    operations_.insert(id, {promise, {}});
    queue_->enqueue(
        *this, QString::number(id), [this, id, owner, documents = std::move(documents)] {
            if (!operations_.contains(id))
                return;
            if (!window_) {
                finish(id, false);
                return;
            }
            auto* dialog = new WorkspaceEditReviewDialog(owner, documents, window_);
            operations_[id].dialog = dialog;
            connect(dialog, &QDialog::finished, this,
                    [this, id](int result) { finish(id, result == QDialog::Accepted); });
            connect(dialog, &QObject::destroyed, this, [this, id] { finish(id, false); });
            dialog->open();
        });
    return future;
}
void WorkspaceEditReviewService::finish(quint64 id, bool accepted) {
    const auto operation = operations_.take(id);
    if (!operation.promise)
        return;
    if (queue_)
        queue_->release(*this, QString::number(id));
    operation.promise->addResult(accepted);
    operation.promise->finish();
}
void WorkspaceEditReviewService::cancel(quint64 id) {
    const auto dialog = operations_.value(id).dialog;
    // Remove the operation before closing its dialog: finished/destroyed can
    // synchronously call finish again. Complete the promise only once.
    finish(id, false);
    if (dialog)
        dialog->reject();
}
} // namespace litecode::ui
