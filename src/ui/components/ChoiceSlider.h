#pragma once
#include "ui/components/Controls.h"
class QDialog;
class QLabel;
class QSlider;
class QHBoxLayout;
namespace litecode::ui::components {
class IconButton;
// Shared discrete-choice popup. Knows labels and indices, never language/model semantics.
class ChoiceSlider final : public ComboBox {
  public:
    explicit ChoiceSlider(QWidget* parent = nullptr);
    QWidget* popupWidget() const override;
    QWidget* leadingActionsParent() const;
    QHBoxLayout* leadingActionsLayout() const;
    QWidget* contextActionsParent() const;
    QHBoxLayout* contextActionsLayout() const;
    void setDescription(const QString& text);
    void setDefaultIndex(int index);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    QString displayText() const override;
    void showPopup() override;
    void hidePopup() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void synchronize();
    void choose(int index);
    QDialog* choicePopup_{};
    QLabel *title_{}, *description_{};
    QSlider* slider_{};
    IconButton* reset_{};
    QWidget* leading_{};
    QHBoxLayout* leadingLayout_{};
    QWidget* context_{};
    QHBoxLayout* contextLayout_{};
    int defaultIndex_{};
    bool synchronizing_{};
};
} // namespace litecode::ui::components
