#pragma once

#include <QDialog>
#include <QString>

namespace litecode::ui {

enum class ConfirmationChoice {
    Primary,
    Secondary,
    Cancel,
};

class ConfirmationDialog final : public QDialog {
  public:
    ConfirmationDialog(const QString& title, const QString& message, const QString& primaryText,
                       const QString& secondaryText, const QString& cancelText,
                       QWidget* parent = nullptr);

    [[nodiscard]] ConfirmationChoice choice() const;

    static ConfirmationChoice ask(QWidget* parent, const QString& title, const QString& message,
                                  const QString& primaryText, const QString& secondaryText = {},
                                  const QString& cancelText = {});
    static void showMessage(QWidget* parent, const QString& title, const QString& message);

  private:
    ConfirmationChoice choice_{ConfirmationChoice::Cancel};
};

} // namespace litecode::ui
