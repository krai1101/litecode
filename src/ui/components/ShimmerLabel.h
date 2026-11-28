#pragma once
#include "ui/components/ElidingLabel.h"
#include <QElapsedTimer>
#include <QTimer>
namespace litecode::ui::components {
// Lightweight text-only shimmer. It inherits the surrounding body font and
// never changes layout or asks the extension host to publish animation frames.
class ShimmerLabel final : public ElidingLabel {
  public:
    explicit ShimmerLabel(const QString& text, QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;

  private:
    QTimer animation_;
    QElapsedTimer clock_;
};
} // namespace litecode::ui::components
