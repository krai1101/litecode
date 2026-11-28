#pragma once
#include <QDialog>
#include <optional>
class QCheckBox;
namespace litecode::ui {
class UriConfirmationDialog final : public QDialog {
  public:
    UriConfirmationDialog(const QString& title, const QString& message, const QString& details,
                          const std::optional<QString>& rememberLabel, QWidget* parent);
    bool remember() const;

  private:
    QCheckBox* remember_{};
};
} // namespace litecode::ui
