#pragma once
#include "extensions/WorkbenchInputBox.h"
#include <QObject>
#include <QPointer>
class QWidget;
namespace litecode::ui {
class InputBoxSession;
class InputBoxService final : public QObject {
  public:
    InputBoxService(extensions::WorkbenchInputBox& boxes, QWidget* window, QObject* parent);
    ~InputBoxService() override;

  private:
    QPointer<extensions::WorkbenchInputBox> boxes_;
    QPointer<QWidget> window_;
    QPointer<InputBoxSession> active_;
};
} // namespace litecode::ui
