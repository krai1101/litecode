#pragma once

#include <QDockWidget>

namespace litecode::ui {

class WorkbenchDock final : public QDockWidget {
    Q_OBJECT

  public:
    WorkbenchDock(const QString& title, bool showRightBorder, QWidget* parent = nullptr);
  signals:
    void closedByUser();

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

  private:
    void positionRightBorder();

    QWidget* rightBorder_{};
};

} // namespace litecode::ui
