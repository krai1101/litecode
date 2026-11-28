#pragma once
#include <QPixmap>
#include <QPointer>
#include <QWidget>
namespace litecode::ui {
// Full-colour user images are decoded off the GUI thread at thumbnail size.
class NativeImageView final : public QWidget {
    Q_OBJECT
  public:
    NativeImageView(const QString& path, const QString& description, int extent, QWidget* parent);
    bool imageReady() const { return !image_.isNull(); }
    void setOverlay(QWidget* action);
    QSize sizeHint() const override { return QSize(maximumWidth(), height()); }
    QSize minimumSizeHint() const override { return QSize(0, height()); }

  protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

  private:
    QPixmap image_;
    bool loading_{true};
    QPointer<QWidget> overlay_;
};
} // namespace litecode::ui
