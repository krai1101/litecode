#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>

class QDialog;
class QEvent;
class QAbstractItemDelegate;
class QFrame;
class QListView;
class QPaintEvent;
class QWidget;

namespace litecode::ui::components {

enum class ButtonVariant {
    Primary,
    Secondary,
    Ghost,
    Danger,
};

enum class InputState {
    Default,
    Error,
};

class Button : public QPushButton {
  public:
    explicit Button(const QString& text, QWidget* parent = nullptr,
                    ButtonVariant variant = ButtonVariant::Secondary);
};

class IconButton : public QToolButton {
  public:
    explicit IconButton(QWidget* parent = nullptr, ButtonVariant variant = ButtonVariant::Ghost);
};

class Input : public QLineEdit {
  public:
    explicit Input(QWidget* parent = nullptr);
};

class ComboBox : public QComboBox {
  public:
    explicit ComboBox(QWidget* parent = nullptr);
    void setPopupItemDelegate(QAbstractItemDelegate* delegate);
    [[nodiscard]] QWidget* popupWidget() const;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showPopup() override;
    void hidePopup() override;

  private:
    void choosePopupIndex(const QModelIndex& index);

    QFrame* popup_{};
    QListView* popupView_{};
    bool hidingPopup_{};
};

// Compact editor embedded inside an existing row. Unlike Input, it intentionally does not
// receive the standard form-control minimum height or padding.
class InlineInput : public QLineEdit {
  public:
    explicit InlineInput(QWidget* parent = nullptr);
};

class List : public QListWidget {
  public:
    explicit List(QWidget* parent = nullptr);
};

class Tree : public QTreeWidget {
  public:
    explicit Tree(QWidget* parent = nullptr);
};

class Menu : public QMenu {
  public:
    explicit Menu(QWidget* parent = nullptr);
    Menu(const QString& title, QWidget* parent = nullptr);
};

void setButtonVariant(QPushButton& button, ButtonVariant variant);
void setButtonVariant(QToolButton& button, ButtonVariant variant);
void setInputState(QLineEdit& input, InputState state);
void adoptDialogControls(QDialog& dialog);

} // namespace litecode::ui::components
