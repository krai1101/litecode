#include "ui/components/QuickInputDialog.h"
#include "ui/PopupPositioner.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"
#include "ui/components/PopupSurface.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QShowEvent>
#include <QStyle>
#include <QVBoxLayout>

namespace litecode::ui::components {
QuickInputDialog::QuickInputDialog(const QString& title, const QString& prompt,
                                   const QString& placeholder, QWidget* parent)
    : QDialog(parent) {
    setObjectName("gitInputDialog");
    setWindowTitle(title);
    PopupSurface::configureDialog(*this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    auto* surface = new PopupSurface(this);
    title_ = new QLabel(surface);
    title_->setTextFormat(Qt::PlainText);
    title_->setAlignment(Qt::AlignCenter);
    title_->hide();
    surface->contentLayout()->addWidget(title_);
    input_ = new Input(surface);
    input_->setObjectName("gitInput");
    input_->setAccessibleName(prompt);
    input_->setPlaceholderText(placeholder.isEmpty() ? prompt : placeholder);
    auto* inputRow = new QHBoxLayout;
    inputRow_ = inputRow;
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(2);
    inputRow->addWidget(input_, 1);
    surface->contentLayout()->addLayout(inputRow);
    auto* hint = new QLabel(tr("%1 (Enter to confirm, Escape to cancel)").arg(prompt), surface);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    hint_ = hint;
    surface->contentLayout()->addWidget(hint);
    validation_ = new QLabel(surface);
    validation_->setObjectName("quickInputValidation");
    validation_->setTextFormat(Qt::PlainText);
    validation_->setWordWrap(true);
    validation_->hide();
    surface->contentLayout()->addWidget(validation_);
    layout->addWidget(surface);
    connect(input_, &QLineEdit::returnPressed, this, &QuickInputDialog::accept);
}
void QuickInputDialog::accept() {
    if (acceptanceHandler_) {
        const auto handler = acceptanceHandler_;
        handler();
        return;
    }
    if ((rawInput_ || !value().isEmpty()) && !pending_ && !blocksAcceptance_)
        QDialog::accept();
}
QString QuickInputDialog::value() const {
    return rawInput_ ? input_->text() : input_->text().trimmed();
}
void QuickInputDialog::setValidation(const QString& diagnostic, bool pending) {
    setValidationSeverity(diagnostic, diagnostic.isEmpty() ? 0 : 3, pending);
}
void QuickInputDialog::setValidationSeverity(const QString& diagnostic, int severity,
                                             bool pending) {
    pending_ = pending;
    blocksAcceptance_ = severity == 3;
    setProperty("validationPending", pending);
    validation_->setProperty("validationSeverity", severity);
    validation_->style()->unpolish(validation_);
    validation_->style()->polish(validation_);
    validation_->setText(diagnostic);
    validation_->setVisible(!diagnostic.isEmpty());
    setInputState(*input_, severity == 3   ? InputState::Error
                           : severity == 2 ? InputState::Warning
                           : severity == 1 ? InputState::Info
                                           : InputState::Default);
    adjustSize();
}
void QuickInputDialog::setHint(const QString& hint) {
    hint_->setText(hint);
    hint_->setVisible(!hint.isEmpty());
}
void QuickInputDialog::setTitleText(const QString& title) {
    title_->setText(title);
    title_->setVisible(!title.isEmpty());
}
void QuickInputDialog::setRawInputMode(bool enabled) { rawInput_ = enabled; }
void QuickInputDialog::setAcceptanceHandler(std::function<void()> handler) {
    acceptanceHandler_ = std::move(handler);
}
QToolButton* QuickInputDialog::addInputAction(const QString& icon, const QString& tooltip) {
    auto* button = new IconButton(input_->parentWidget());
    setThemedIcon(button, icon);
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    inputRow_->addWidget(button);
    return button;
}
void QuickInputDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    auto* owner = parentWidget() ? parentWidget()->window() : nullptr;
    setFixedWidth(PopupPositioner::boundedWidth(owner, 636, 16));
    adjustSize();
    PopupPositioner::placeTopCentered(*this, owner, 30, 16);
    input_->setFocus(Qt::PopupFocusReason);
}
} // namespace litecode::ui::components
