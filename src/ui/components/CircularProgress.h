#pragma once
#include <QProgressBar>
class QFrame;
class QLabel;
namespace litecode::ui::components {
// A determinate meter. It never animates or implies that work is loading.
class CircularProgress final : public QProgressBar {
  public:
    explicit CircularProgress(QWidget* parent = nullptr);
    void setDetails(const QString& text);

  protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QFrame* tip_{};
    QLabel* details_{};
};
} // namespace litecode::ui::components
