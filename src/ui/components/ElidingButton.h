#pragma once
#include "ui/components/Controls.h"
namespace litecode::ui::components {
// A flexible single-line action. Long paths retain their filename.
class ElidingButton final : public Button {
  public:
    using Button::Button;
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent*) override;
};
} // namespace litecode::ui::components
