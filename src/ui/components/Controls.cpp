#include "ui/components/Controls.h"
#include "ui/components/ComponentTokens.h"
#include "ui/components/IconLabelButton.h"
#include "ui/components/PopupSurface.h"
#include <QRegularExpression>

#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/WorkbenchChrome.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionViewItem>
#include <QStylePainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>

namespace litecode::ui::components {
namespace {

void refresh(QWidget& widget);

class DropdownDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        // initStyleOption normalizes newlines for Qt's single text layout.
        // Keep the original title/description boundary for our two-part rows.
        const auto lines = index.data(Qt::DisplayRole).toString().split('\n');
        const QFontMetrics metrics(item.font);
        const int inset = textInset() + 28 +
                          (item.icon.isNull() ? 0 : qBound(8, item.decorationSize.width(), 16) + 6);
        int naturalWidth = 0;
        for (const auto& line : lines)
            naturalWidth = qMax(naturalWidth, metrics.horizontalAdvance(line));
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setWidth(qMax(size.width(), naturalWidth + inset));
        if (lines.size() > 1) {
            const auto* view = qobject_cast<const QAbstractItemView*>(parent());
            const int width = view ? view->viewport()->width() : option.rect.width();
            const int descriptionHeight =
                metrics
                    .boundingRect(QRect(0, 0, qMax(1, width - inset), 10000), Qt::TextWordWrap,
                                  lines.mid(1).join(' '))
                    .height();
            size.setHeight(metrics.height() + 2 + descriptionHeight + 4);
        }
        return size;
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const auto text = index.data(Qt::DisplayRole).toString();
        const auto icon = item.icon;
        item.text.clear();
        item.icon = {};
        const auto* style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
        const auto tokens = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
        const bool enabled = index.flags() & Qt::ItemIsEnabled;
        const auto custom = index.data(Qt::ForegroundRole).value<QColor>();
        painter->save();
        painter->setFont(item.font);
        painter->setPen(!enabled                                ? tokens.disabledForeground
                        : custom.isValid()                      ? custom
                        : (item.state & QStyle::State_Selected) ? tokens.listSelectionForeground
                                                                : tokens.dropdownForeground);
        auto content = option.rect.adjusted(textInset(), 0, -28, 0);
        if (!icon.isNull()) {
            const int extent = qBound(8, item.decorationSize.width(), 16);
            icon.paint(painter,
                       QRect(content.left(), content.center().y() - extent / 2, extent, extent),
                       Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
            content.setLeft(content.left() + extent + 6);
        }
        const auto lines = text.split('\n');
        if (lines.size() > 1) {
            content.adjust(0, 2, 0, -2);
            const int height = QFontMetrics(item.font).height();
            painter->drawText(
                content.adjusted(0, 0, 0, -(content.height() - height)),
                Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(item.font).elidedText(lines.first(), Qt::ElideRight, content.width()));
            painter->setPen(custom.isValid() ? custom : tokens.mutedForeground);
            painter->drawText(content.adjusted(0, height + 2, 0, 0),
                              Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                              lines.mid(1).join(' '));
        } else {
            painter->drawText(
                content, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(item.font).elidedText(text, Qt::ElideRight, qMax(0, content.width())));
        }
        if (item.widget && index.row() == item.widget->property("choiceSelectedIndex").toInt())
            tintedIcon(":/icons/check.svg", custom.isValid() ? custom : tokens.foreground,
                       custom.isValid() ? custom : tokens.foreground)
                .paint(painter,
                       QRect(option.rect.right() - 26, option.rect.center().y() - 8, 16, 16));
        painter->restore();
    }

  private:
    int textInset() const {
        const ComponentMetrics metrics;
        return parent()->property("menuRows").toBool() ? metrics.horizontalPadding
                                                       : metrics.spacingMedium;
    }
};

class ComboPopupFrame final : public QFrame {
  public:
    using QFrame::QFrame;

  protected:
    void paintEvent(QPaintEvent*) override {
        const bool dark = qApp != nullptr && qApp->property("litecodeDarkTheme").toBool();
        const auto tokens = resolveComponentTokens(Theme::tokens(dark));
        const bool menu = property("searchableMenu").toBool();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(menu ? tokens.menu.background : tokens.dropdown.listBackground);
        painter.setPen(menu ? tokens.menu.border : tokens.dropdown.listBorder);
        const int radius = property("descriptivePopup").toBool() ? 16 : tokens.metrics.radiusMedium;
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), radius, radius);
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

void Button::paintEvent(QPaintEvent* event) {
    if (!textCentered_ || icon().isNull()) {
        QPushButton::paintEvent(event);
        return;
    }
    QStyleOptionButton option;
    initStyleOption(&option);
    const auto label = option.text;
    auto glyph = option.icon;
    option.text.clear();
    option.icon = {};
    QPainter painter(this);
    style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
    const auto content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
    const auto shown = fontMetrics().elidedText(
        label, Qt::ElideRight, qMax(0, content.width() - 2 * (iconSize().width() + 4)));
    style()->drawItemText(&painter, content, Qt::AlignCenter, option.palette, isEnabled(), shown,
                          QPalette::ButtonText);
    if (!isEnabled()) {
        const auto resource = property("litecodeIconResource").toString();
        if (!resource.isEmpty()) {
            const auto color =
                Theme::tokens(qApp->property("litecodeDarkTheme").toBool()).disabledForeground;
            glyph = tintedIcon(resource, color, color);
        }
    }
    const QRect iconRect(
        content.center().x() - fontMetrics().horizontalAdvance(shown) / 2 - iconSize().width() - 4,
        content.center().y() - iconSize().height() / 2, iconSize().width(), iconSize().height());
    glyph.paint(&painter, iconRect, Qt::AlignCenter, QIcon::Normal);
}

IconButton::IconButton(QWidget* parent, ButtonVariant variant) : QToolButton(parent) {
    setButtonVariant(*this, variant);
}

Input::Input(QWidget* parent) : QLineEdit(parent) { assignRole(*this, "input"); }

InputFrame::InputFrame(QWidget* parent) : QWidget(parent) {
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget* previous, QWidget* current) {
        const bool hadFocus = previous && isAncestorOf(previous);
        const bool hasFocus = current && isAncestorOf(current);
        if (hadFocus != hasFocus)
            update();
    });
}

void InputFrame::paintEvent(QPaintEvent*) {
    const auto theme = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
    const auto* focused = QApplication::focusWidget();
    const auto border =
        focused && isAncestorOf(focused) ? theme.searchFocusBorder : theme.inputBorder;
    QPainter painter(this);
    const auto transform = painter.deviceTransform();
    const auto bounds = transform.mapRect(QRectF(rect()));
    // Center the cosmetic pen on device pixels so fractional scaling keeps it crisp.
    const QRectF deviceStroke(QPointF(qRound(bounds.left()) + .5, qRound(bounds.top()) + .5),
                              QPointF(qRound(bounds.right()) - .5, qRound(bounds.bottom()) - .5));
    QPen pen(border, 1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(theme.inputSurface);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawRoundedRect(transform.inverted().mapRect(deviceStroke), 3, 3);
}

ComboBox::ComboBox(QWidget* parent) : QComboBox(parent) {
    setProperty("modernComboBox", true);
    setStyle(sharedWorkbenchStyle());
    setCursor(Qt::PointingHandCursor);
    setMaxVisibleItems(12);
    setMinimumWidth(100);
    setProperty("litecodeChevronResource", ":/icons/chevron-down.svg");
    assignRole(*this, "input");

    popup_ =
        new ComboPopupFrame(this, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    popup_->setObjectName(QStringLiteral("litecodeComboPopup"));
    popup_->setAttribute(Qt::WA_TranslucentBackground);
    popup_->installEventFilter(this);
    assignRole(*popup_, "comboPopup");
    auto* layout = new QVBoxLayout(popup_);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);
    popupHeader_ = new QWidget(popup_);
    popupHeader_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* header = new QHBoxLayout(popupHeader_);
    header->setContentsMargins(10, 4, 10, 4);
    popupTitle_ = new QLabel(popupHeader_);
    popupTitle_->setTextFormat(Qt::PlainText);
    popupTitle_->setWordWrap(true);
    popupTitle_->setContentsMargins(0, 0, 0, 0);
    popupTitle_->hide();
    header->addWidget(popupTitle_, 1);
    popupHelp_ = new QPushButton(popupHeader_);
    popupHelp_->setFlat(true);
    popupHelp_->setCursor(Qt::PointingHandCursor);
    popupHelp_->setObjectName("comboPopupHelp");
    auto helpFont = popupHelp_->font();
    helpFont.setUnderline(true);
    helpFont.setWeight(QFont::Normal);
    popupHelp_->setFont(helpFont);
    popupHelp_->setStyleSheet("QPushButton { background: transparent; border: 0; padding: 0; }");
    header->addWidget(popupHelp_);
    popupHelp_->hide();
    popupHeader_->hide();
    connect(popupHelp_, &QPushButton::clicked, this, [this] {
        if (helpUrl_.scheme() == "https")
            QDesktopServices::openUrl(helpUrl_);
    });
    layout->addWidget(popupHeader_);
    popupView_ = new QListView(popup_);
    popupView_->setItemDelegate(new DropdownDelegate(popupView_));
    // The popup is sized to its rows. QListView's default minimum height
    // otherwise enlarges short menus after their upward position is computed.
    popupView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    popupView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    popupView_->setSelectionMode(QAbstractItemView::SingleSelection);
    popupView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    popupView_->setTextElideMode(Qt::ElideRight);
    popupView_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    popupView_->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    popupView_->installEventFilter(this);
    popupView_->viewport()->setCursor(Qt::PointingHandCursor);
    assignRole(*popupView_, "list");
    layout->addWidget(popupView_, 1);
    QScrollBar* scrollBar = popupView_->verticalScrollBar();
    scrollBar->setProperty("scrollbarActive", true);
    refresh(*scrollBar);
    connect(popupView_, &QListView::clicked, this, &ComboBox::choosePopupIndex);
}

void ComboBox::setPopupSearch(const QString& placeholder) {
    if (!popupSearch_) {
        popup_->setProperty("searchableMenu", true);
        popupView_->setProperty("menuRows", true);
        refresh(*popup_);
        refresh(*popupView_);
        popupSearch_ = new Input(popup_);
        popupSearch_->setObjectName("comboPopupSearch");
        popupSearch_->installEventFilter(this);
        auto* layout = static_cast<QVBoxLayout*>(popup_->layout());
        layout->insertWidget(0, popupSearch_);
        layout->insertSpacing(1, ComponentMetrics{}.spacingSmall);
        connect(popupSearch_, &QLineEdit::textChanged, this, &ComboBox::filterPopupChoices);
    }
    popupSearch_->setPlaceholderText(placeholder);
    popupSearch_->setAccessibleName(placeholder);
}

QPushButton* ComboBox::addPopupAction(const QString& label, std::function<void()> callback) {
    if (!popupActions_) {
        popupActions_ = new QWidget(popup_);
        auto* actions = new QVBoxLayout(popupActions_);
        const int gap = ComponentMetrics{}.spacingSmall;
        actions->setContentsMargins(0, gap, 0, 0);
        actions->setSpacing(0);
        auto* separator = new QFrame(popupActions_);
        assignRole(*separator, "menuSeparator");
        separator->setFixedHeight(1);
        actions->addWidget(separator);
        actions->addSpacing(gap);
        static_cast<QVBoxLayout*>(popup_->layout())->addWidget(popupActions_);
    }
    auto* button = new QPushButton(label, popupActions_);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    assignRole(*button, "menuAction");
    popupActions_->layout()->addWidget(button);
    connect(button, &QPushButton::clicked, this, [this, callback = std::move(callback)] {
        hidePopup();
        callback();
    });
    return button;
}

void ComboBox::clearPopupActions() {
    if (!popupActions_)
        return;
    popup_->layout()->removeWidget(popupActions_);
    popupActions_->hide();
    popupActions_->deleteLater();
    popupActions_ = nullptr;
}

int ComboBox::popupExtraHeight() const {
    const auto height = [](const QWidget* widget) {
        return widget && !widget->isHidden() ? widget->sizeHint().height() : 0;
    };
    const int searchHeight =
        popupSearch_ ? height(popupSearch_) + ComponentMetrics{}.spacingSmall : 0;
    return height(popupHeader_) + searchHeight + height(popupActions_);
}

void ComboBox::filterPopupChoices(const QString& query) {
    if (popupView_->model() != model())
        return;
    QModelIndex match;
    for (int row = 0; row < count(); ++row) {
        const auto index = model()->index(row, modelColumn());
        const bool visible = itemText(row).contains(query.trimmed(), Qt::CaseInsensitive);
        popupView_->setRowHidden(row, !visible);
        if (visible && !match.isValid() && (index.flags() & Qt::ItemIsEnabled) &&
            (index.flags() & Qt::ItemIsSelectable))
            match = index;
    }
    const auto current = popupView_->currentIndex();
    if (!current.isValid() || popupView_->isRowHidden(current.row()))
        popupView_->setCurrentIndex(match);
}

void ComboBox::setPopupItemDelegate(QAbstractItemDelegate* delegate) {
    popupView_->setItemDelegate(delegate);
}

void ComboBox::setDisplayItems(const QStringList& items) {
    if (property("displayItems").toStringList() == items && count() == items.size())
        return;
    setProperty("displayItems", items);
    popup_->setProperty("descriptivePopup",
                        std::any_of(items.cbegin(), items.cend(),
                                    [](const QString& item) { return item.contains('\n'); }));
    clear();
    for (const auto& text : items)
        addItem(IconLabelButton::stripIcons(text).trimmed());
    refreshChoiceIcons();
}
void ComboBox::refreshChoiceIcons() {
    const auto items = property("displayItems").toStringList();
    const auto tokens = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
    const QRegularExpression iconPattern(R"(\$\(([A-Za-z0-9-]+)\))");
    for (int i = 0; i < qMin(count(), int(items.size())); ++i) {
        const QColor custom(itemColors_.value(i));
        const auto color = custom.isValid() ? custom : tokens.foreground;
        setItemData(i, custom.isValid() ? QVariant(custom) : QVariant(), Qt::ForegroundRole);
        if (!iconPaths_.value(i).isEmpty()) {
            setItemIcon(i, tintedIcon(iconPaths_[i], color, color));
            continue;
        }
        const auto match = iconPattern.match(items[i].section('\n', 0, 0));
        if (match.hasMatch())
            setItemIcon(i, IconLabelButton::themeIcon(match.captured(1), color));
    }
}
void ComboBox::changeEvent(QEvent* event) {
    QComboBox::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        refreshChoiceIcons();
}
void ComboBox::setPopupTitle(const QString& title) {
    popupTitle_->setText(title);
    popupTitle_->setVisible(!title.isEmpty());
    popupHeader_->setVisible(!title.isEmpty() || !popupHelp_->isHidden());
}
void ComboBox::setChoicePresentation(const QStringList& icons, const QStringList& colors) {
    iconPaths_ = icons;
    itemColors_ = colors;
    refreshChoiceIcons();
}
void ComboBox::setPopupHelp(const QString& text, const QUrl& url) {
    helpUrl_ = url;
    popupHelp_->setText(text);
    popupHelp_->setVisible(!text.isEmpty() && url.scheme() == "https");
    popupHeader_->setVisible(!popupTitle_->text().isEmpty() || !popupHelp_->isHidden());
}
QSize ComboBox::sizeHint() const {
    auto size = QComboBox::sizeHint();
    if (iconOnly_) {
        size.setWidth(28);
        return size;
    }
    if (currentText().contains('\n')) {
        int width = 0;
        for (int i = 0; i < count(); ++i)
            width = qMax(width, fontMetrics().horizontalAdvance(itemText(i).section('\n', 0, 0)));
        size.setWidth(width + (itemIcon(currentIndex()).isNull() ? 28 : 50));
    }
    return size;
}
QSize ComboBox::minimumSizeHint() const {
    if (iconOnly_)
        return sizeHint();
    return currentText().contains('\n') ? sizeHint() : QComboBox::minimumSizeHint();
}
QWidget* ComboBox::popupWidget() const { return popup_; }
void ComboBox::setIconOnly(bool enabled) {
    if (iconOnly_ == enabled)
        return;
    iconOnly_ = enabled;
    updateGeometry();
    update();
}

bool ComboBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == popup_ && event->type() == QEvent::Hide && !hidingPopup_)
        QComboBox::hidePopup();
    if ((watched == popupView_ || watched == popupSearch_) && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            hidePopup();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            choosePopupIndex(popupView_->currentIndex());
            return true;
        }
        if (watched == popupSearch_ && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            const int step = key->key() == Qt::Key_Down ? 1 : -1;
            const auto current = popupView_->currentIndex();
            int row = current.isValid() ? current.row() + step : (step > 0 ? 0 : count() - 1);
            for (; row >= 0 && row < count(); row += step) {
                const auto index = model()->index(row, modelColumn());
                if (!popupView_->isRowHidden(row) && (index.flags() & Qt::ItemIsEnabled) &&
                    (index.flags() & Qt::ItemIsSelectable)) {
                    popupView_->setCurrentIndex(index);
                    popupView_->scrollTo(index);
                    break;
                }
            }
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void ComboBox::choosePopupIndex(const QModelIndex& index) {
    if (!index.isValid() || index.row() >= count() || !(index.flags() & Qt::ItemIsEnabled) ||
        !(index.flags() & Qt::ItemIsSelectable))
        return;
    setCurrentIndex(index.row());
    hidePopup();
    emit activated(index.row());
    emit textActivated(currentText());
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
    popupView_->setIconSize(iconSize());
    if (popupSearch_) {
        if (popupSearch_->text().isEmpty())
            filterPopupChoices({});
        else
            popupSearch_->clear();
    }
    // A plain searchable list uses uniform rows. Only wrapping or custom rows
    // need to be measured again after the popup has acquired its final width.
    const bool uniform = popupSearch_ && !popup_->property("descriptivePopup").toBool() &&
                         dynamic_cast<const DropdownDelegate*>(popupView_->itemDelegate());
    popupView_->setUniformItemSizes(uniform);
    const int rowHeight = qMax(22, popupView_->sizeHintForRow(0));
    const int desiredHeight = qMin(count(), maxVisibleItems()) * rowHeight + 8 + popupExtraHeight();
    int desiredWidth = qMax(width(), popupView_->sizeHintForColumn(modelColumn()) + 48);
    QStyleOptionViewItem rowOption;
    rowOption.initFrom(popupView_);
    rowOption.decorationSize = iconSize();
    for (int row = 0; !uniform && row < count(); ++row)
        desiredWidth =
            qMax(desiredWidth, popupView_->itemDelegate()
                                       ->sizeHint(rowOption, model()->index(row, modelColumn()))
                                       .width() +
                                   8 + 2 * popupView_->frameWidth());
    for (const auto* extra : {static_cast<QWidget*>(popupHeader_),
                              static_cast<QWidget*>(popupSearch_), popupActions_}) {
        if (extra && !extra->isHidden())
            desiredWidth = qMax(desiredWidth, extra->sizeHint().width() + 8);
    }
    popup_->setGeometry(PopupSurface::anchoredGeometry(*this, QSize(desiredWidth, desiredHeight),
                                                       popupWithinWindow_, popupWithinWindow_));
    popupView_->setProperty("choiceSelectedIndex", currentIndex());
    popupView_->setCurrentIndex(model()->index(currentIndex(), modelColumn()));
    popup_->show();
    const int visibleRows = qMin(count(), maxVisibleItems());
    // Width clamping can wrap descriptions. Measure after the actual viewport
    // width settles, then remeasure once if removing a scrollbar frees space.
    for (int pass = 0; !uniform && pass < 2; ++pass) {
        popup_->layout()->activate();
        popupView_->doItemsLayout();
        const QRect first = popupView_->visualRect(model()->index(0, modelColumn()));
        const QRect last = popupView_->visualRect(model()->index(visibleRows - 1, modelColumn()));
        const int rowsHeight = last.bottom() - first.top() + 1;
        const int chromeHeight = popup_->height() - popupView_->viewport()->height();
        const int exactHeight = rowsHeight + chromeHeight;
        popup_->setGeometry(PopupSurface::anchoredGeometry(*this, QSize(desiredWidth, exactHeight),
                                                           popupWithinWindow_, popupWithinWindow_));
    }
    popup_->layout()->activate();
    popupView_->doItemsLayout();
    popupView_->scrollTo(popupView_->currentIndex(), QAbstractItemView::EnsureVisible);
    auto* focus = popupSearch_ ? static_cast<QWidget*>(popupSearch_) : popupView_;
    focus->setFocus(Qt::PopupFocusReason);
}

void ComboBox::mousePressEvent(QMouseEvent* event) {
    if (popupSearch_ && !isEditable() && event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }
    QComboBox::mousePressEvent(event);
}

void ComboBox::mouseReleaseEvent(QMouseEvent* event) {
    if (popupSearch_ && !isEditable() && event->button() == Qt::LeftButton) {
        if (rect().contains(event->position().toPoint()))
            showPopup();
        event->accept();
        return;
    }
    QComboBox::mouseReleaseEvent(event);
}

void ComboBox::hidePopup() {
    hidingPopup_ = true;
    popup_->hide();
    QComboBox::hidePopup();
    hidingPopup_ = false;
    if (isVisible() && isEnabled()) {
        window()->activateWindow();
        setFocus(Qt::PopupFocusReason);
    }
}

QString ComboBox::displayText() const { return currentText().section('\n', 0, 0); }
void ComboBox::paintEvent(QPaintEvent* event) {
    if (iconOnly_ && !isEditable()) {
        QPainter painter(this);
        const QSize extent = iconSize().boundedTo(QSize(16, 16));
        const QRect bounds(QPoint((width() - extent.width()) / 2, (height() - extent.height()) / 2),
                           extent);
        itemIcon(currentIndex())
            .paint(&painter, bounds, Qt::AlignCenter,
                   isEnabled() ? QIcon::Normal : QIcon::Disabled);
        return;
    }
    if ((currentText().contains('\n') || displayText() != currentText()) && !isEditable()) {
        QStylePainter caption(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QColor selectedColor(itemColors_.value(currentIndex()));
        if (selectedColor.isValid()) {
            option.palette.setColor(QPalette::Text, selectedColor);
            option.palette.setColor(QPalette::ButtonText, selectedColor);
            option.palette.setColor(QPalette::WindowText, selectedColor);
        }
        option.currentText = displayText();
        // Prefer an identifiable icon to a chopped caption on compact toolbars.
        if (!itemIcon(currentIndex()).isNull() &&
            width() < fontMetrics().horizontalAdvance(option.currentText) + 50)
            option.currentText.clear();
        caption.drawComplexControl(QStyle::CC_ComboBox, option);
        caption.drawControl(QStyle::CE_ComboBoxLabel, option);
    } else {
        QComboBox::paintEvent(event);
    }
    if (!chevronVisible_)
        return;

    QPainter painter(this);
    const bool dark = qApp != nullptr && qApp->property("litecodeDarkTheme").toBool();
    const auto tokens = Theme::tokens(dark);
    const QColor selectedColor(itemColors_.value(currentIndex()));
    const auto color = !isEnabled()              ? tokens.disabledForeground
                       : selectedColor.isValid() ? selectedColor
                                                 : tokens.dropdownForeground;
    tintedIcon(property("litecodeChevronResource").toString(), color, color)
        .paint(&painter, QRect(width() - 22, height() / 2 - 8, 16, 16));
}

void ComboBox::keyPressEvent(QKeyEvent* event) {
    if ((!isEditable() && (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return ||
                           event->key() == Qt::Key_Enter)) ||
        ((event->modifiers() & Qt::AltModifier) &&
         (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up))) {
        showPopup();
        event->accept();
        return;
    }
    QComboBox::keyPressEvent(event);
}

InlineInput::InlineInput(QWidget* parent) : QLineEdit(parent) { assignRole(*this, "inlineInput"); }

List::List(QWidget* parent) : QListWidget(parent) { assignRole(*this, "list"); }
CheckBox::CheckBox(QWidget* parent) : QCheckBox(parent) { assignRole(*this, "checkbox"); }

Tree::Tree(QWidget* parent) : QTreeWidget(parent) { assignRole(*this, "tree"); }

Menu::Menu(QWidget* parent) : QMenu(parent) {
    setMinimumWidth(160);
    setStyle(sharedWorkbenchStyle());
    setWindowFlag(Qt::NoDropShadowWindowHint);
    assignRole(*this, "menu");
}

Menu::Menu(const QString& title, QWidget* parent) : Menu(parent) { setTitle(title); }

void setButtonVariant(QPushButton& button, ButtonVariant variant) {
    assignRole(button, "button", variantName(variant));
}

void setButtonVariant(QToolButton& button, ButtonVariant variant) {
    assignRole(button, "iconButton", variantName(variant));
}

void setInputState(QLineEdit& input, InputState state) {
    const auto role = state == InputState::Error     ? "error"
                      : state == InputState::Warning ? "warning"
                      : state == InputState::Info    ? "info"
                                                     : "default";
    input.setProperty("uiState", QString::fromLatin1(role));
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
