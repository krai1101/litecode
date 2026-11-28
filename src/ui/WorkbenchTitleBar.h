#pragma once

#include <QWidget>

class QMenuBar;
class QToolButton;

namespace litecode::ui {

class WorkbenchTitleBar final : public QWidget {
    Q_OBJECT

  public:
    WorkbenchTitleBar(QMenuBar* menuBar, QWidget* window);
    ~WorkbenchTitleBar() override;

    [[nodiscard]] bool containsInteractivePoint(const QPoint& point) const;
    void setDarkTheme(bool dark);
    void updateWindowState();

  signals:
    void togglePrimarySidebarRequested();
    void toggleBottomPanelRequested();
    void toggleSecondarySidebarRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
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
    QToolButton* close_{};
};

} // namespace litecode::ui
