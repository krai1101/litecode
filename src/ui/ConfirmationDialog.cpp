#include "ui/ConfirmationDialog.h"
#include "ui/components/Controls.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace litecode::ui {

ConfirmationDialog::ConfirmationDialog(const QString& title, const QString& message,
                                       const QString& primaryText, const QString& secondaryText,
                                       const QString& cancelText, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("confirmationDialog"));
    setWindowTitle(title);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);

    outer->addWidget(new DialogHeader(title, *this, this));

    auto* separator = new QFrame(this);
    separator->setObjectName(QStringLiteral("confirmationSeparator"));
    separator->setFixedHeight(1);
    outer->addWidget(separator);

    auto* body = new QWidget(this);
    body->setObjectName(QStringLiteral("confirmationBody"));
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(16, 16, 16, 14);
    bodyLayout->setSpacing(18);
    auto* messageLabel = new QLabel(message, body);
    messageLabel->setObjectName(QStringLiteral("confirmationMessage"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    messageLabel->setCursor(Qt::IBeamCursor);
    bodyLayout->addWidget(messageLabel);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);

    auto addButton = [this, buttons](const QString& text, const QString& objectName,
                                     ConfirmationChoice choice) -> QPushButton* {
        if (text.isEmpty())
            return nullptr;
        auto* button = new components::Button(text, this);
        button->setObjectName(objectName);
        connect(button, &QPushButton::clicked, this, [this, choice] {
            choice_ = choice;
            accept();
        });
        buttons->addWidget(button);
        return button;
    };

    auto* primary =
        addButton(primaryText, QStringLiteral("confirmationPrimary"), ConfirmationChoice::Primary);
    addButton(secondaryText, QStringLiteral("confirmationSecondary"),
              ConfirmationChoice::Secondary);
    addButton(cancelText, QStringLiteral("confirmationCancel"), ConfirmationChoice::Cancel);
    if (primary != nullptr) {
        DialogChrome::markPrimary(primary);
        primary->setDefault(true);
        primary->setFocus(Qt::TabFocusReason);
    }
    bodyLayout->addLayout(buttons);
    outer->addWidget(body);
    DialogChrome::configure(*this);
    DialogChrome::fitToOwner(*this, QSize(440, sizeHint().height()), QSize(320, 160));
}

ConfirmationChoice ConfirmationDialog::choice() const { return choice_; }

ConfirmationChoice ConfirmationDialog::ask(QWidget* parent, const QString& title,
                                           const QString& message, const QString& primaryText,
                                           const QString& secondaryText,
                                           const QString& cancelText) {
    ConfirmationDialog dialog(title, message, primaryText, secondaryText, cancelText, parent);
    (void)dialog.exec();
    return dialog.choice();
}

void ConfirmationDialog::showMessage(QWidget* parent, const QString& title,
                                     const QString& message) {
    (void)ask(parent, title, message, tr("OK"));
}

} // namespace litecode::ui
