#pragma once
#include "extensions/WorkbenchStatusBar.h"
#include <QPointer>
#include <QSet>
#include <QPoint>
class QStatusBar;
class QHBoxLayout;
class QWidget;
namespace litecode::ui {
class StatusBarService final : public QObject {
  public:
    StatusBarService(extensions::WorkbenchStatusBar& backend, QStatusBar& bar, QObject* parent);
    ~StatusBarService() override;
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
  private:
    void refresh();
    void showMenu(const QPoint& position);
    QPointer<extensions::WorkbenchStatusBar> backend_;
    QPointer<QStatusBar> bar_;
    QPointer<QWidget> left_, right_;
    QHBoxLayout* leftLayout_{};
    QHBoxLayout* rightLayout_{};
    QHash<QString, QPointer<QWidget>> buttons_;
    QSet<QString> hidden_;
};
}
