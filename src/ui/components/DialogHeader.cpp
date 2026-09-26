#include "ui/components/DialogHeader.h"

#include "ui/components/Controls.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>

namespace litecode::ui {

DialogHeader::DialogHeader(const QString& title, QDialog& dialog, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("dialogHeader"));
    setProperty("uiComponent", QStringLiteral("dialogHeader"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(36);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 4, 0);
    layout->setSpacing(8);
    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("dialogTitle"));
    titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(titleLabel, 1);

    auto* close = new components::IconButton(this, components::ButtonVariant::Danger);
    close->setObjectName(QStringLiteral("dialogClose"));
    close->setFixedSize(28, 28);
    close->setText(QStringLiteral("×"));
    close->setToolButtonStyle(Qt::ToolButtonTextOnly);
    close->setToolTip(tr("Close"));
    close->setAccessibleName(tr("Close"));
    layout->addWidget(close, 0, Qt::AlignVCenter);
    connect(close, &QToolButton::clicked, &dialog, &QDialog::reject);
}

} // namespace litecode::ui
