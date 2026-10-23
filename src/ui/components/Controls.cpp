#include "ui/components/Controls.h"

#include "ui/Theme.h"
#include "ui/WorkbenchChrome.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>

namespace litecode::ui::components {
namespace {

void refresh(QWidget& widget);

class ComboPopupFrame final : public QFrame {
  public:
    using QFrame::QFrame;

  protected:
    void paintEvent(QPaintEvent*) override {
        const bool dark = qApp != nullptr && qApp->property("litecodeDarkTheme").toBool();
        const ThemeTokens tokens = Theme::tokens(dark);
        QPainter painter(this);
        painter.fillRect(rect(), tokens.dropdownListSurface);
        const QColor border = tokens.dropdownListBorder;
        painter.fillRect(0, 0, width(), 1, border);
        painter.fillRect(0, height() - 1, width(), 1, border);
        painter.fillRect(0, 0, 1, height(), border);
        painter.fillRect(width() - 1, 0, 1, height(), border);
    }
};

QString variantName(ButtonVariant variant) {
    switch (variant) {
    case ButtonVariant::Primary:
        return QStringLiteral("primary");
    case ButtonVariant::Ghost:
        return QStringLiteral("ghost");
    case ButtonVariant::Danger:
        return QStringLiteral("danger");
    case ButtonVariant::Secondary:
    default:
        return QStringLiteral("secondary");
    }
}

void assignRole(QWidget& widget, const char* component, const QString& variant = {}) {
    widget.setProperty("uiComponent", QString::fromLatin1(component));
    if (!variant.isEmpty())
        widget.setProperty("uiVariant", variant);
    refresh(widget);
}

void refresh(QWidget& widget) {
    if (widget.style() == nullptr)
        return;
    widget.style()->unpolish(&widget);
    widget.style()->polish(&widget);
    widget.update();
}

} // namespace

Button::Button(const QString& text, QWidget* parent, ButtonVariant variant)
    : QPushButton(text, parent) {
    setButtonVariant(*this, variant);
}

IconButton::IconButton(QWidget* parent, ButtonVariant variant) : QToolButton(parent) {
    setButtonVariant(*this, variant);
}

Input::Input(QWidget* parent) : QLineEdit(parent) { assignRole(*this, "input"); }

ComboBox::ComboBox(QWidget* parent) : QComboBox(parent) {
    setProperty("modernComboBox", true);
    setStyle(sharedWorkbenchStyle());
    setCursor(Qt::PointingHandCursor);
    setMaxVisibleItems(7);
    assignRole(*this, "input");

    popup_ =
        new ComboPopupFrame(this, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    popup_->setObjectName(QStringLiteral("litecodeComboPopup"));
    popup_->setAttribute(Qt::WA_OpaquePaintEvent);
    popup_->installEventFilter(this);
    assignRole(*popup_, "comboPopup");
    auto* layout = new QVBoxLayout(popup_);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    popupView_ = new QListView(popup_);
    popupView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    popupView_->setSelectionMode(QAbstractItemView::SingleSelection);
    popupView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popupView_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    popupView_->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    popupView_->installEventFilter(this);
    popupView_->viewport()->setCursor(Qt::PointingHandCursor);
    assignRole(*popupView_, "list");
    layout->addWidget(popupView_);
    QScrollBar* scrollBar = popupView_->verticalScrollBar();
    scrollBar->setProperty("scrollbarActive", true);
    refresh(*scrollBar);
    connect(popupView_, &QListView::clicked, this, &ComboBox::choosePopupIndex);
}

void ComboBox::setPopupItemDelegate(QAbstractItemDelegate* delegate) {
    popupView_->setItemDelegate(delegate);
}

QWidget* ComboBox::popupWidget() const { return popup_; }

bool ComboBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == popup_ && event->type() == QEvent::Hide && !hidingPopup_)
        QComboBox::hidePopup();
    if (watched == popupView_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            hidePopup();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            choosePopupIndex(popupView_->currentIndex());
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void ComboBox::choosePopupIndex(const QModelIndex& index) {
    if (index.isValid() && index.row() < count())
        setCurrentIndex(index.row());
    hidePopup();
}

void ComboBox::showPopup() {
    if (popup_->isVisible()) {
        hidePopup();
        return;
    }
    if (count() == 0)
        return;
    if (popupView_->model() != model())
        popupView_->setModel(model());
    popupView_->setModelColumn(modelColumn());
    const int rowHeight = qMax(28, popupView_->sizeHintForRow(0));
    const int desiredHeight = qMin(count(), maxVisibleItems()) * rowHeight + 4;
    QScreen* targetScreen = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (targetScreen == nullptr)
        return;
    const QRect screenGeometry = targetScreen->availableGeometry();
    const QPoint below = mapToGlobal(QPoint(0, height() + 1));
    const QPoint above = mapToGlobal(QPoint(0, -1));
    const int belowSpace = screenGeometry.bottom() - below.y() + 1;
    const int aboveSpace = above.y() - screenGeometry.top() + 1;
    const bool openAbove = belowSpace < rowHeight * 2 && aboveSpace > belowSpace;
    const int availableHeight = openAbove ? aboveSpace : belowSpace;
    const int popupHeight = qMin(desiredHeight, qMax(1, availableHeight));
    const int popupWidth = qMin(width(), screenGeometry.width());
    const int popupX =
        qBound(screenGeometry.left(), below.x(), screenGeometry.right() - popupWidth + 1);
    popup_->setGeometry(popupX, openAbove ? above.y() - popupHeight : below.y(), popupWidth,
                        popupHeight);
    popupView_->setCurrentIndex(model()->index(currentIndex(), modelColumn()));
    popup_->show();
    popupView_->doItemsLayout();
    const int visibleRows = qMin(count(), maxVisibleItems());
    const QRect firstRow = popupView_->visualRect(model()->index(0, modelColumn()));
    const QRect lastRow = popupView_->visualRect(model()->index(visibleRows - 1, modelColumn()));
    if (firstRow.isValid() && lastRow.isValid()) {
        const int exactHeight = lastRow.bottom() - firstRow.top() + 1 + 2;
        const int fittedHeight = qMin(exactHeight, qMax(1, availableHeight));
        popup_->setGeometry(popupX, openAbove ? above.y() - fittedHeight : below.y(), popupWidth,
                            fittedHeight);
    }
    popupView_->scrollTo(popupView_->currentIndex(), QAbstractItemView::PositionAtTop);
    popupView_->setFocus(Qt::PopupFocusReason);
}

void ComboBox::hidePopup() {
    hidingPopup_ = true;
    popup_->hide();
    QComboBox::hidePopup();
    hidingPopup_ = false;
}

void ComboBox::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = qApp != nullptr && qApp->property("litecodeDarkTheme").toBool();
    QPen pen(Theme::tokens(dark).mutedForeground);
    pen.setWidthF(1.3);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    const QPoint center(width() - 16, height() / 2);
    QPainterPath chevron;
    chevron.moveTo(center + QPoint(-5, -2));
    chevron.lineTo(center + QPoint(0, 3));
    chevron.lineTo(center + QPoint(5, -2));
    painter.drawPath(chevron);
}

InlineInput::InlineInput(QWidget* parent) : QLineEdit(parent) {}

List::List(QWidget* parent) : QListWidget(parent) { assignRole(*this, "list"); }

Tree::Tree(QWidget* parent) : QTreeWidget(parent) { assignRole(*this, "tree"); }

Menu::Menu(QWidget* parent) : QMenu(parent) {
    setStyle(sharedWorkbenchStyle());
    setWindowFlag(Qt::NoDropShadowWindowHint);
    assignRole(*this, "menu");
}

Menu::Menu(const QString& title, QWidget* parent) : QMenu(title, parent) {
    setStyle(sharedWorkbenchStyle());
    setWindowFlag(Qt::NoDropShadowWindowHint);
    assignRole(*this, "menu");
}

void setButtonVariant(QPushButton& button, ButtonVariant variant) {
    assignRole(button, "button", variantName(variant));
}

void setButtonVariant(QToolButton& button, ButtonVariant variant) {
    assignRole(button, "iconButton", variantName(variant));
}

void setInputState(QLineEdit& input, InputState state) {
    input.setProperty("uiState", state == InputState::Error ? QStringLiteral("error")
                                                            : QStringLiteral("default"));
    refresh(input);
}

void adoptDialogControls(QDialog& dialog) {
    assignRole(dialog, "dialog");

    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (!button->property("uiComponent").isValid())
            setButtonVariant(*button, ButtonVariant::Secondary);
    }
    for (auto* input : dialog.findChildren<QLineEdit*>()) {
        if (qobject_cast<QComboBox*>(input->parentWidget()) != nullptr)
            continue;
        if (!input->property("uiComponent").isValid())
            assignRole(*input, "input");
    }
    for (auto* combo : dialog.findChildren<QComboBox*>())
        assignRole(*combo, "input");
    for (auto* spinBox : dialog.findChildren<QAbstractSpinBox*>())
        assignRole(*spinBox, "input");
    for (auto* keySequence : dialog.findChildren<QKeySequenceEdit*>())
        assignRole(*keySequence, "input");
    for (auto* view : dialog.findChildren<QAbstractItemView*>()) {
        if (!view->property("uiComponent").isValid())
            assignRole(*view, "list");
    }
    for (auto* checkBox : dialog.findChildren<QCheckBox*>())
        assignRole(*checkBox, "checkbox");
    for (auto* group : dialog.findChildren<QGroupBox*>())
        assignRole(*group, "group");
    for (auto* actions : dialog.findChildren<QDialogButtonBox*>())
        assignRole(*actions, "dialogActions");
}

} // namespace litecode::ui::components
