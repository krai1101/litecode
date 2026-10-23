#pragma once

#include <QFrame>

class QDialog;
class QVBoxLayout;

namespace litecode::ui {

class PopupSurface final : public QFrame {
  public:
    explicit PopupSurface(QWidget* parent = nullptr);

    [[nodiscard]] QVBoxLayout* contentLayout() const;
    static void configureDialog(QDialog& dialog);

  private:
    QVBoxLayout* contentLayout_{};
};

} // namespace litecode::ui
