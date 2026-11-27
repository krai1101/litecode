#pragma once
#include "extensions/WorkbenchQuickPick.h"
#include <QPointer>
class QWidget;
namespace litecode::ui {
class QuickPickSession;
class QuickPickService final : public QObject {
  public:
    QuickPickService(extensions::WorkbenchQuickPick& backend, QWidget* window, QObject* parent);
    ~QuickPickService() override;
  private:
    QPointer<extensions::WorkbenchQuickPick> backend_;
    QPointer<QWidget> window_;
    QPointer<QuickPickSession> active_;
};
}
