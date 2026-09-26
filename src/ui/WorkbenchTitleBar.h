#pragma once

#include <QWidget>

class QMenuBar;
class QToolButton;

namespace litecode::ui {

class WorkbenchTitleBar final : public QWidget {
    Q_OBJECT

  public:
    WorkbenchTitleBar(QMenuBar* menuBar, QWidget* window);

    [[nodiscard]] bool containsInteractivePoint(const QPoint& point) const;
    void setDarkTheme(bool dark);
    void updateWindowState();

  signals:
    void togglePrimarySidebarRequested();
    void toggleBottomPanelRequested();

  protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void layoutChrome();
    void toggleMaximizeRestore();
    void updateButtonIcon(QToolButton* button);

    QWidget* window_{};
    QMenuBar* menuBar_{};
    QWidget* controls_{};
    QToolButton* maximize_{};
};

} // namespace litecode::ui
