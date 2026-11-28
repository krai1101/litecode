#include "ui/GitCloneProgressDialog.h"
#include "ui/components/Controls.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"
#include "workspace/GitService.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace litecode::ui {
GitCloneProgressDialog::GitCloneProgressDialog(workspace::GitService* git, QWidget* parent)
    : QDialog(parent), git_(git) {
    setObjectName("gitCloneProgressDialog");
    setWindowTitle(tr("Cloning Repository"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);
    outer->addWidget(new DialogHeader(tr("Cloning Repository"), *this, this));
    auto* separator = new QFrame(this);
    separator->setObjectName("confirmationSeparator");
    separator->setFixedHeight(1);
    outer->addWidget(separator);
    auto* body = new QWidget(this);
    body->setObjectName("confirmationBody");
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(12);
    auto* label = new QLabel(tr("Cloning with Git..."), body);
    label->setObjectName("confirmationMessage");
    layout->addWidget(label);
    auto* progress = new QProgressBar(body);
    progress->setObjectName("gitCloneProgress");
    progress->setRange(0, 0);
    progress->setTextVisible(false);
    layout->addWidget(progress);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel = new components::Button(tr("Cancel"), body);
    cancel->setObjectName("gitCloneCancel");
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    outer->addWidget(body);
    connect(cancel, &QPushButton::clicked, this, [this, cancel] {
        cancel->setEnabled(false);
        git_->cancelClone();
    });
    connect(git_, &workspace::GitService::busyChanged, this, [this](bool busy) {
        if (!busy)
            accept();
    });
    DialogChrome::configure(*this);
    DialogChrome::fitToOwner(*this, QSize(400, sizeHint().height()), QSize(320, 150));
}

void GitCloneProgressDialog::reject() {
    git_->cancelClone();
    if (!git_->busy())
        QDialog::reject();
}
} // namespace litecode::ui
