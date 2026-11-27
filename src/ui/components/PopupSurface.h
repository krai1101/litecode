#pragma once

#include <QFrame>

class QDialog;
class QVBoxLayout;

namespace litecode::ui {

class PopupSurface final : public QFrame {
  public:
    explicit PopupSurface(QWidget* parent = nullptr, qreal shadowStrength = 1.0);

    [[nodiscard]] QVBoxLayout* contentLayout() const;
    static void configureDialog(QDialog& dialog);
    static QRect anchoredGeometry(const QWidget& anchor, QSize desired, bool withinWindow = true,
                                  bool preferAbove = true,
                                  Qt::AlignmentFlag horizontal = Qt::AlignLeft);

  private:
    QVBoxLayout* contentLayout_{};
};

} // namespace litecode::ui
