#pragma once
#include "extensions/WorkbenchProgress.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QElapsedTimer>
class QStatusBar;
class QToolButton;
class QTimer;
namespace litecode::ui {
class StatusProgressService final : public QObject {
    Q_OBJECT
  public:
    StatusProgressService(extensions::WorkbenchProgress& progress, QStatusBar& bar, QObject* parent);
    ~StatusProgressService() override;
    void setNotificationVisible(const QString& owner, quint64 id, bool visible);
  signals:
    void revealRequested(const QString& owner, quint64 id);
  private:
    struct Task {
        extensions::ProgressTaskInfo info;
        qint64 eligibleAt{}, shownAt{-1};
        quint64 order{};
        bool ended{}, notificationVisible{};
    };
    void refresh();
    QHash<QString, Task> tasks_;
    QPointer<QToolButton> button_;
    QTimer* timer_{};
    QElapsedTimer clock_;
    quint64 nextOrder_{};
    QString selected_;
};
} // namespace litecode::ui
