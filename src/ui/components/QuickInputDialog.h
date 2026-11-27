#pragma once
#include <QDialog>
#include <functional>

class QLineEdit;
class QShowEvent;
class QLabel;
class QToolButton;
class QHBoxLayout;

namespace litecode::ui::components {
// A single-value quick input using the same native surface and placement as Quick Open.
class QuickInputDialog final : public QDialog {
    Q_OBJECT
  public:
    QuickInputDialog(const QString& title, const QString& prompt, const QString& placeholder,
                     QWidget* parent = nullptr);
    QString value() const;
    QLineEdit* input() const { return input_; }
    void setValidation(const QString& diagnostic, bool pending = false);
    void setHint(const QString& hint);
    void setTitleText(const QString& title);
    void setRawInputMode(bool enabled);
    void setAcceptanceHandler(std::function<void()> handler);
    void setValidationSeverity(const QString& diagnostic, int severity, bool pending = false);
    QToolButton* addInputAction(const QString& icon, const QString& tooltip);
    void accept() override;

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    QLineEdit* input_{};
    QLabel* hint_{};
    QLabel* title_{};
    QLabel* validation_{};
    bool pending_{};
    bool rawInput_{};
    bool blocksAcceptance_{};
    std::function<void()> acceptanceHandler_;
    QHBoxLayout* inputRow_{};
};
} // namespace litecode::ui::components
