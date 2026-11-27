#pragma once
#include "extensions/WorkbenchProgress.h"
#include <QFrame>
class QLabel;
class QProgressBar;
class QPushButton;
namespace litecode::ui {
// Presentation only: task completion and cancellation belong to the broker.
class ProgressNotification final : public QFrame {
    Q_OBJECT
  public:
    explicit ProgressNotification(const extensions::ProgressTaskInfo& task, QWidget* parent);
    void updateTask(const extensions::ProgressTaskInfo& task);
  signals:
    void cancelRequested();
    void dismissRequested();
    void manageExtensionRequested();
  private:
    QLabel* text_{};
    QProgressBar* progress_{};
    QPushButton* cancel_{};
};
} // namespace litecode::ui
