#pragma once
#include <QWidget>
namespace litecode::ui {
// A compact row still needs the height of its wrapped children at its actual
// width. QWidget's unconstrained size hint is not a valid fixed-height limit.
class NativeContentRow final : public QWidget {
  public:
    using QWidget::QWidget;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
};
} // namespace litecode::ui
