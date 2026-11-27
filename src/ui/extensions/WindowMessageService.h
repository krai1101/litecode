#pragma once
#include "extensions/WorkbenchMessages.h"
#include "extensions/WorkbenchProgress.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QVector>

class QWidget;
class QScrollArea;
class QVBoxLayout;
namespace litecode::ui {
class ModalDialogQueue;
class ProgressNotification;
class WindowMessageService final : public QObject {
    Q_OBJECT
  public:
    WindowMessageService(extensions::WorkbenchMessages& messages, QWidget* owner, QObject* parent);
    ~WindowMessageService() override;
    void attachProgress(extensions::WorkbenchProgress& progress);
    void revealProgress(const QString& owner, quint64 id);
  signals:
    void manageExtensionRequested(const QString& id);
    void progressVisibilityChanged(const QString& owner, quint64 id, bool visible);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void show(const extensions::WindowMessageRequest& request);
    void showModal(const extensions::WindowMessageRequest& request);
    void dismiss(const QString& owner, quint64 id);
    void positionNotifications();
    void choose(const QString& owner, quint64 id, std::optional<int> selection);
    void showProgress(const extensions::ProgressTaskInfo& task);
    void dismissProgress(const QString& owner, quint64 id);
    QPointer<extensions::WorkbenchProgress> progress_;
    QHash<QString, QPointer<ProgressNotification>> progressViews_;
    QPointer<extensions::WorkbenchMessages> messages_;
    QPointer<QWidget> owner_;
    QPointer<QScrollArea> notifications_;
    QPointer<QWidget> notificationBody_;
    QVBoxLayout* notificationLayout_{};
    struct View {
        QString owner;
        quint64 id{};
        QPointer<QWidget> widget;
        bool modal{};
    };
    QVector<View> views_;
    QPointer<ModalDialogQueue> modalQueue_;
};
} // namespace litecode::ui
