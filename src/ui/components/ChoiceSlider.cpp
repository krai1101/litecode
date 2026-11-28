#include "ui/components/ChoiceSlider.h"
#include "ui/ThemedIcon.h"
#include "ui/components/PopupSurface.h"
#include <QAbstractItemModel>
#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
namespace litecode::ui::components {
ChoiceSlider::ChoiceSlider(QWidget* parent) : ComboBox(parent) {
    choicePopup_ = new QDialog(this);
    PopupSurface::configureDialog(*choicePopup_);
    choicePopup_->setObjectName("choiceSliderPopup");
    choicePopup_->installEventFilter(this);
    auto* outer = new QVBoxLayout(choicePopup_);
    outer->setContentsMargins(8, 8, 8, 8);
    auto* surface = new PopupSurface(choicePopup_);
    outer->addWidget(surface);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(6, 4, 6, 0);
    leading_ = new QWidget(surface);
    leadingLayout_ = new QHBoxLayout(leading_);
    leadingLayout_->setContentsMargins(0, 0, 0, 0);
    header->addWidget(leading_);
    auto* labels = new QVBoxLayout;
    labels->setSpacing(2);
    title_ = new QLabel(surface);
    title_->setProperty("uiComponent", "choiceTitle");
    description_ = new QLabel(surface);
    description_->setTextFormat(Qt::PlainText);
    description_->setProperty("uiComponent", "choiceDescription");
    title_->setTextFormat(Qt::PlainText);
    title_->setAlignment(Qt::AlignCenter);
    description_->setAlignment(Qt::AlignCenter);
    labels->addWidget(title_);
    labels->addWidget(description_);
    context_ = new QWidget(surface);
    contextLayout_ = new QHBoxLayout(context_);
    contextLayout_->setContentsMargins(0, 0, 0, 0);
    contextLayout_->setAlignment(Qt::AlignHCenter);
    labels->addWidget(context_);
    header->addLayout(labels, 1);
    reset_ = new IconButton(surface);
    reset_->setObjectName("choiceSliderReset");
    reset_->setAccessibleName(tr("Reset to default"));
    setThemedIcon(reset_, ":/icons/refresh.svg");
    header->addWidget(reset_);
    surface->contentLayout()->addLayout(header);
    slider_ = new QSlider(Qt::Horizontal, surface);
    slider_->setObjectName("choiceSlider");
    slider_->setProperty("uiComponent", "choiceSlider");
    slider_->setTickPosition(QSlider::TicksBelow);
    slider_->setTickInterval(1);
    slider_->setPageStep(1);
    slider_->setSingleStep(1);
    slider_->setMinimumHeight(36);
    slider_->installEventFilter(this);
    surface->contentLayout()->addWidget(slider_);
    connect(reset_, &QAbstractButton::clicked, this, [this] { choose(defaultIndex_); });
    connect(this, &QComboBox::currentIndexChanged, this, [this] { synchronize(); });
    connect(slider_, &QSlider::valueChanged, this, [this](int index) {
        title_->setText(itemText(index));
        if (!synchronizing_ && !slider_->isSliderDown())
            choose(index);
    });
    connect(slider_, &QSlider::sliderReleased, this, [this] { choose(slider_->value()); });
}
QWidget* ChoiceSlider::popupWidget() const { return choicePopup_; }
QWidget* ChoiceSlider::leadingActionsParent() const { return leading_; }
QHBoxLayout* ChoiceSlider::leadingActionsLayout() const { return leadingLayout_; }
QWidget* ChoiceSlider::contextActionsParent() const { return context_; }
QHBoxLayout* ChoiceSlider::contextActionsLayout() const { return contextLayout_; }
QString ChoiceSlider::displayText() const {
    return description_->text().isEmpty() ? currentText()
                                          : description_->text() + " " + currentText();
}
QSize ChoiceSlider::sizeHint() const {
    auto size = ComboBox::sizeHint();
    size.setWidth(fontMetrics().horizontalAdvance(displayText()) + 28);
    return size;
}
QSize ChoiceSlider::minimumSizeHint() const {
    auto size = sizeHint();
    size.setWidth(60);
    return size;
}
void ChoiceSlider::setDescription(const QString& text) {
    description_->setText(text);
    synchronize();
    updateGeometry();
    update();
}
void ChoiceSlider::setDefaultIndex(int index) {
    defaultIndex_ = index;
    synchronize();
}
void ChoiceSlider::synchronize() {
    synchronizing_ = true;
    const QSignalBlocker block(slider_);
    slider_->setRange(0, qMax(0, count() - 1));
    slider_->setValue(qBound(0, currentIndex(), qMax(0, count() - 1)));
    slider_->setEnabled(count() > 1);
    title_->setText(currentText());
    slider_->setAccessibleName(currentText());
    reset_->setEnabled(defaultIndex_ >= 0 && defaultIndex_ < count() &&
                       currentIndex() != defaultIndex_);
    leading_->setVisible(leadingLayout_->count() > 0);
    context_->setVisible(contextLayout_->count() > 0);
    description_->setVisible(contextLayout_->count() == 0 && !description_->text().isEmpty());
    synchronizing_ = false;
}
void ChoiceSlider::choose(int index) {
    if (index < 0 || index >= count() ||
        !(model()->index(index, modelColumn()).flags() & Qt::ItemIsEnabled))
        return;
    setCurrentIndex(index);
    synchronize();
    emit activated(index);
    emit textActivated(currentText());
}
void ChoiceSlider::showPopup() {
    if (choicePopup_->isVisible()) {
        hidePopup();
        return;
    }
    if (count() == 0)
        return;
    synchronize();
    choicePopup_->ensurePolished();
    choicePopup_->layout()->activate();
    const auto minimum = choicePopup_->minimumSizeHint();
    choicePopup_->setGeometry(PopupSurface::anchoredGeometry(
        *this, QSize(qMax(320, minimum.width()), qMax(144, minimum.height())), true, true));
    choicePopup_->show();
    slider_->setFocus(Qt::PopupFocusReason);
}
void ChoiceSlider::hidePopup() {
    choicePopup_->hide();
    ComboBox::hidePopup();
}
bool ChoiceSlider::eventFilter(QObject* watched, QEvent* event) {
    if (watched == choicePopup_ && event->type() == QEvent::Hide)
        QComboBox::hidePopup();
    if (watched == slider_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            hidePopup();
            return true;
        }
        if (key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return) {
            choose(slider_->value());
            hidePopup();
            return true;
        }
    }
    return ComboBox::eventFilter(watched, event);
}
} // namespace litecode::ui::components
