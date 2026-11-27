#pragma once
#include "extensions/WindowStateApi.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QHash>
class QWidget;
namespace litecode::extensions { class ExtensionManager; }
namespace litecode::ui {
class WindowStateService final : public QObject {
  public:
    WindowStateService(extensions::ExtensionManager& manager, QWidget& window, QObject* parent = nullptr);
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
  private:
    bool belongsToWindow(QWidget* widget) const;
    void refreshFocus();
    void activity();
    void scheduleInactive();
    void publish();
    QPointer<extensions::ExtensionManager> manager_;
    QPointer<QWidget> window_;
    QTimer interval_;
    QTimer inactive_;
    struct ProgressLease { QPointer<QTimer> delay; bool held{}; };
    QHash<QString, QHash<quint64, ProgressLease>> progressLeases_;
    bool inputHeld_{true};
    int progressHeld_{};
    extensions::WindowState state_{false, true};
    int intervalsWithoutActivity_{};
};
}
