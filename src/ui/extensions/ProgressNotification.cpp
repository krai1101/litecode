#include "ui/extensions/ProgressNotification.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"
#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace litecode::ui {
ProgressNotification::ProgressNotification(const extensions::ProgressTaskInfo& task,
                                           QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("extensionNotificationFrame"));
    setProperty("progressOwner", task.owner);
    setProperty("progressId", QString::number(task.id));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("progressMessage"));
    text_->setTextFormat(Qt::PlainText);
    text_->setWordWrap(true);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header->addWidget(text_, 1);
    auto* manage = new components::IconButton(this);
    setThemedIcon(manage, QStringLiteral(":/icons/more.svg"));
    manage->setAccessibleName(tr("Notification actions"));
    manage->setToolTip(tr("Notification actions"));
    auto* menu = new components::Menu(manage);
    connect(menu->addAction(tr("Manage Extension")), &QAction::triggered, this,
            &ProgressNotification::manageExtensionRequested);
    manage->setMenu(menu);
    manage->setPopupMode(QToolButton::InstantPopup);
    header->addWidget(manage);
    auto* close = new components::IconButton(this);
    setThemedIcon(close, QStringLiteral(":/icons/close.svg"));
    close->setAccessibleName(tr("Dismiss notification"));
    close->setToolTip(tr("Dismiss notification"));
    connect(close, &QToolButton::clicked, this, &ProgressNotification::dismissRequested);
    header->addWidget(close);
    layout->addLayout(header);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("extensionNotificationProgress"));
    progress_->setTextVisible(false);
    progress_->setAccessibleName(tr("Task progress"));
    layout->addWidget(progress_);
    auto* source = new QLabel(task.source, this);
    source->setTextFormat(Qt::PlainText);
    layout->addWidget(source);
    if (task.options.cancellable.value_or(false)) {
        cancel_ = new components::Button(tr("Cancel"), this);
        cancel_->setObjectName(QStringLiteral("progressCancel"));
        connect(cancel_, &QPushButton::clicked, this, &ProgressNotification::cancelRequested);
        layout->addWidget(cancel_);
    }
    updateTask(task);
}
void ProgressNotification::updateTask(const extensions::ProgressTaskInfo& task) {
    const auto title = task.options.title.value_or(QString());
    const auto message = task.message.value_or(QString());
    const auto text = title.isEmpty()     ? message
                      : message.isEmpty() ? title
                                          : title + ": " + message;
    text_->setText(text);
    setAccessibleName(text);
    if (task.total) {
        progress_->setRange(0, 1000);
        progress_->setValue(static_cast<int>(std::round(std::clamp(*task.total, 0.0, 100.0) * 10)));
    } else
        progress_->setRange(0, 0);
    if (cancel_)
        cancel_->setEnabled(!task.cancellationRequested);
}
} // namespace litecode::ui
