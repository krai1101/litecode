#pragma once

#include "ui/extensions/WorkspaceEditReview.h"
#include <QDialog>
#include <QVector>

namespace litecode::ui {
class WorkspaceEditReviewDialog final : public QDialog {
  public:
    WorkspaceEditReviewDialog(const QString& owner,
                              const QVector<WorkspaceEditReviewDocument>& documents,
                              QWidget* parent);
};
} // namespace litecode::ui
