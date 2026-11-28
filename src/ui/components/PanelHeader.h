#pragma once
#include <QWidget>
class QStackedWidget;
class QPlainTextEdit;
namespace litecode::ui::components {
class IconLabelButton;
// One workbench-owned header for every bottom-panel page. Content contributes
// tools, never its own close/maximize chrome or another panel window.
class PanelHeader final : public QWidget {
    Q_OBJECT
  public:
    explicit PanelHeader(QWidget* parent = nullptr);
    void setTools(QWidget* tools);
    void setMaximized(bool maximized);
  signals:
    void closeRequested();
    void maximizeRequested();

  private:
    QStackedWidget* tools_{};
    QWidget* empty_{};
    IconLabelButton* maximize_{};
};
void filterPanelText(QPlainTextEdit& text, const QString& filter);
QWidget* createTextPanelTools(QPlainTextEdit& text, const QString& name, QWidget* parent);
} // namespace litecode::ui::components
