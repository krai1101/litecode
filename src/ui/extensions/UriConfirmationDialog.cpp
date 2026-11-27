#include "ui/extensions/UriConfirmationDialog.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"
#include "ui/components/Controls.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
namespace litecode::ui {
UriConfirmationDialog::UriConfirmationDialog(const QString& title, const QString& message, const QString& details,
    const std::optional<QString>& rememberLabel, QWidget* parent) : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose); DialogChrome::configure(*this); setWindowTitle(title);
    auto* layout = new QVBoxLayout(this); layout->addWidget(new DialogHeader(title, *this, this));
    auto* label = new QLabel(message, this); label->setTextFormat(Qt::PlainText); label->setWordWrap(true); layout->addWidget(label);
    auto* detail = new QLabel(details, this); detail->setTextFormat(Qt::PlainText); detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(detail);
    if (rememberLabel) {
        remember_ = new components::CheckBox(this); remember_->setText(*rememberLabel); layout->addWidget(remember_);
    }
    auto* actions = new QDialogButtonBox(this);
    DialogChrome::markPrimary(actions->addButton(tr("Open"), QDialogButtonBox::AcceptRole));
    actions->addButton(QDialogButtonBox::Cancel); layout->addWidget(actions);
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    components::adoptDialogControls(*this); DialogChrome::fitToOwner(*this, QSize(460, 250));
}
bool UriConfirmationDialog::remember() const { return remember_ && remember_->isChecked(); }
}
