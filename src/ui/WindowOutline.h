#pragma once

#include <QWidget>

namespace litecode::ui {

class WindowOutline final : public QWidget {
  public:
    explicit WindowOutline(QWidget* parent);

    void setDarkTheme(bool dark);
    void syncToWindow(bool maximized);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    bool dark_{};
};

} // namespace litecode::ui
