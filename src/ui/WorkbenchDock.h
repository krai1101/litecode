#pragma once

#include <QDockWidget>

namespace litecode::ui {

class WorkbenchDock final : public QDockWidget {
    Q_OBJECT

  public:
    WorkbenchDock(const QString& title, bool showRightBorder, QWidget* parent = nullptr);

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    void positionRightBorder();

    QWidget* rightBorder_{};
};

} // namespace litecode::ui
