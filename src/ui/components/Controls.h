#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <functional>

class QDialog;
class QEvent;
class QAbstractItemDelegate;
class QFrame;
class QListView;
class QLabel;
class QPaintEvent;
class QKeyEvent;
class QMouseEvent;
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
    Warning,
    Info,
};

class Button : public QPushButton {
  public:
    explicit Button(const QString& text, QWidget* parent = nullptr,
                    ButtonVariant variant = ButtonVariant::Secondary);
    void setTextCentered(bool centered) {
        textCentered_ = centered;
        update();
    }

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    bool textCentered_{};
};

class IconButton : public QToolButton {
  public:
    explicit IconButton(QWidget* parent = nullptr, ButtonVariant variant = ButtonVariant::Ghost);
};

class Input : public QLineEdit {
  public:
    explicit Input(QWidget* parent = nullptr);
};

class InputFrame : public QWidget {
  public:
    explicit InputFrame(QWidget* parent = nullptr);

  protected:
    void paintEvent(QPaintEvent* event) override;
};

class ComboBox : public QComboBox {
  public:
    explicit ComboBox(QWidget* parent = nullptr);
    void setPopupItemDelegate(QAbstractItemDelegate* delegate);
    void setPopupTitle(const QString& title);
    void setPopupSearch(const QString& placeholder);
    QPushButton* addPopupAction(const QString& label, std::function<void()> callback);
    void clearPopupActions();
    void setChoicePresentation(const QStringList& iconPaths, const QStringList& colors);
    void setPopupHelp(const QString& text, const QUrl& url);
    void setDisplayItems(const QStringList& items);
    void setIconOnly(bool enabled);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    [[nodiscard]] virtual QWidget* popupWidget() const;
    void setPopupWithinWindow(bool enabled) { popupWithinWindow_ = enabled; }
    void setChevronVisible(bool visible) {
        chevronVisible_ = visible;
        update();
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    virtual QString displayText() const;
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showPopup() override;
    void hidePopup() override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

  private:
    void choosePopupIndex(const QModelIndex& index);
    void filterPopupChoices(const QString& query);
    int popupExtraHeight() const;
    void refreshChoiceIcons();

    QFrame* popup_{};
    QListView* popupView_{};
    QLabel* popupTitle_{};
    QWidget* popupHeader_{};
    QPushButton* popupHelp_{};
    QLineEdit* popupSearch_{};
    QWidget* popupActions_{};
    QUrl helpUrl_;
    QStringList iconPaths_, itemColors_;
    bool hidingPopup_{};
    bool popupWithinWindow_{};
    bool chevronVisible_{true};
    bool iconOnly_{};
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
    using QAbstractScrollArea::setViewportMargins;
};

class CheckBox : public QCheckBox {
  public:
    explicit CheckBox(QWidget* parent = nullptr);
};

class Tree : public QTreeWidget {
  public:
    explicit Tree(QWidget* parent = nullptr);
};

class Menu : public QMenu {
  public:
    explicit Menu(QWidget* parent = nullptr);
    Menu(const QString& title, QWidget* parent = nullptr);
    QAction* addDescriptiveAction(const QString& text);

  protected:
    void paintEvent(QPaintEvent* event) override;
};

void setButtonVariant(QPushButton& button, ButtonVariant variant);
void setButtonVariant(QToolButton& button, ButtonVariant variant);
void setInputState(QLineEdit& input, InputState state);
void adoptDialogControls(QDialog& dialog);

} // namespace litecode::ui::components
