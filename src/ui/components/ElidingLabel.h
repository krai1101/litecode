#pragma once
#include <QLabel>
namespace litecode::ui::components {
// A single-line title. Full text remains available to accessibility and callers.
class ElidingLabel : public QLabel {
  public:
    explicit ElidingLabel(const QString& text, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    void paintEvent(QPaintEvent*) override;
};
} // namespace litecode::ui::components
