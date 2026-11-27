#include "ui/extensions/WindowMessageService.h"
#include "ui/extensions/ProgressNotification.h"
#include "ui/components/Controls.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"
#include "ui/components/ModalDialogQueue.h"
#include "ui/ThemedIcon.h"
#include <QDialog>
#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace litecode::ui {
namespace {
QString title(int severity) {
    switch (severity) {
    case 2: return WindowMessageService::tr("Warning");
    case 3: return WindowMessageService::tr("Error");
    default: return WindowMessageService::tr("Information");
    }
}
QString modalKey(const QString& owner, quint64 id) { return owner + QChar(0) + QString::number(id); }
QLabel* label(const QString& text, QWidget* parent) {
    auto* result = new QLabel(text, parent);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}
} // namespace
WindowMessageService::WindowMessageService(extensions::WorkbenchMessages& messages, QWidget* owner, QObject* parent)
    : QObject(parent), messages_(&messages), owner_(owner) {
    if (owner_) {
        modalQueue_ = &ModalDialogQueue::forWindow(*owner_);
        owner_->installEventFilter(this);
        notifications_ = new QScrollArea(owner_);
        notifications_->setObjectName(QStringLiteral("extensionNotificationCenter"));
        notifications_->setAccessibleName(tr("Extension notifications"));
        notifications_->setFrameShape(QFrame::NoFrame);
        notifications_->setWidgetResizable(true);
        notifications_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        notifications_->setFocusPolicy(Qt::NoFocus);
        notificationBody_ = new QWidget(notifications_);
        notificationLayout_ = new QVBoxLayout(notificationBody_);
        notificationLayout_->setContentsMargins(0, 0, 0, 0);
        notificationLayout_->setSpacing(8);
        notificationLayout_->setAlignment(Qt::AlignTop);
        notifications_->setWidget(notificationBody_);
        notifications_->hide();
    }
    connect(messages_.data(), &extensions::WorkbenchMessages::requested, this,
        [this](const extensions::WindowMessageRequest& request) { show(request); });
    connect(messages_.data(), &extensions::WorkbenchMessages::dismissed, this,
        [this](const QString& owner, quint64 id) { dismiss(owner, id); });
}
WindowMessageService::~WindowMessageService() {
    if (messages_) disconnect(messages_.data(), nullptr, this, nullptr);
    const auto views = std::exchange(views_, {});
    for (const auto& view : views) if (view.widget) {
        disconnect(view.widget, nullptr, this, nullptr);
        delete view.widget.data();
    }
    delete notifications_.data();
}
void WindowMessageService::attachProgress(extensions::WorkbenchProgress& progress) {
    if (progress_ == &progress) return;
    if (progress_) disconnect(progress_.data(), nullptr, this, nullptr);
    const auto oldViews = std::exchange(progressViews_, {});
    for (const auto& view : oldViews) if (view) delete view.data();
    progress_ = &progress;
    connect(&progress, &extensions::WorkbenchProgress::started, this, &WindowMessageService::showProgress);
    connect(&progress, &extensions::WorkbenchProgress::changed, this, [this](const extensions::ProgressTaskInfo& task) {
        const auto card = progressViews_.value(modalKey(task.owner, task.id));
        // Dismissed or cancelled notifications must not reappear on reports.
        if (card) {
            auto presentation = task;
            if (const auto* location = std::get_if<extensions::ProgressLocation>(&task.options.location);
                location && *location == extensions::ProgressLocation::Window) presentation.total.reset();
            card->updateTask(presentation);
        }
        positionNotifications();
    });
    connect(&progress, &extensions::WorkbenchProgress::ended, this, &WindowMessageService::dismissProgress);
    connect(&progress, &QObject::destroyed, this, [this] {
        const auto views = std::exchange(progressViews_, {});
        for (const auto& view : views) if (view) delete view.data();
        positionNotifications();
    });
    positionNotifications();
}
void WindowMessageService::revealProgress(const QString& owner, quint64 id) {
    if (!progress_) return;
    auto task = progress_->task(owner, id);
    if (!task || task->cancellationRequested) return;
    if (const auto* location = std::get_if<extensions::ProgressLocation>(&task->options.location);
        location && *location == extensions::ProgressLocation::Window) {
        task->options.location = extensions::ProgressLocation::Notification;
        task->options.cancellable = false;
        task->total.reset();
    }
    showProgress(*task);
}
void WindowMessageService::showProgress(const extensions::ProgressTaskInfo& task) {
    const auto* location = std::get_if<extensions::ProgressLocation>(&task.options.location);
    if (!location || *location != extensions::ProgressLocation::Notification || !notificationBody_) return;
    const auto key = modalKey(task.owner, task.id);
    if (progressViews_.contains(key)) return;
    auto* card = new ProgressNotification(task, notificationBody_);
    progressViews_.insert(key, card);
    connect(card, &ProgressNotification::manageExtensionRequested, this,
        [this, owner = task.owner] { emit manageExtensionRequested(owner); });
    connect(card, &ProgressNotification::dismissRequested, this,
        [this, owner = task.owner, id = task.id] { dismissProgress(owner, id); });
    connect(card, &ProgressNotification::cancelRequested, this, [this, owner = task.owner, id = task.id] {
        if (progress_ && progress_->cancel(owner, id)) dismissProgress(owner, id);
    });
    notificationLayout_->insertWidget(0, card);
    card->show();
    positionNotifications();
    emit progressVisibilityChanged(task.owner, task.id, true);
}
void WindowMessageService::dismissProgress(const QString& owner, quint64 id) {
    const auto card = progressViews_.take(modalKey(owner, id));
    if (card) {
        notificationLayout_->removeWidget(card);
        card->hide();
        card->deleteLater();
        emit progressVisibilityChanged(owner, id, false);
    }
    positionNotifications();
}
void WindowMessageService::show(const extensions::WindowMessageRequest& request) {
    if (!owner_) { choose(request.owner, request.id, {}); return; }
    if (request.options.modal.value_or(false)) {
        if (modalQueue_) modalQueue_->enqueue(*this, modalKey(request.owner, request.id), [this, request] { showModal(request); });
        else choose(request.owner, request.id, {});
        return;
    }
    // Nonmodal messages are workbench children, not dialogs that block editor
    // input. Detail and close-affordance hints intentionally do not apply here.
    auto* card = new QFrame(notificationBody_);
    card->setObjectName(QStringLiteral("extensionNotificationFrame"));
    card->setAccessibleName(title(request.severity) + ": " + request.text);
    card->setFocusPolicy(Qt::NoFocus);
    card->setProperty("windowMessageOwner", request.owner);
    card->setProperty("windowMessageId", QString::number(request.id));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    header->addWidget(label(title(request.severity), card), 1);
    auto* manage = new components::IconButton(card);
    manage->setObjectName(QStringLiteral("notificationActions"));
    setThemedIcon(manage, QStringLiteral(":/icons/more.svg"));
    manage->setAccessibleName(tr("Notification actions"));
    manage->setToolTip(tr("Notification actions"));
    auto* menu = new components::Menu(manage);
    auto* action = menu->addAction(tr("Manage Extension"));
    connect(action, &QAction::triggered, this, [this, owner = request.owner] { emit manageExtensionRequested(owner); });
    manage->setMenu(menu);
    manage->setPopupMode(QToolButton::InstantPopup);
    header->addWidget(manage);
    auto* close = new components::IconButton(card);
    close->setObjectName(QStringLiteral("notificationClose"));
    setThemedIcon(close, QStringLiteral(":/icons/close.svg"));
    close->setAccessibleName(tr("Dismiss notification"));
    header->addWidget(close);
    connect(close, &QToolButton::clicked, this, [this, request] { choose(request.owner, request.id, {}); });
    layout->addLayout(header);
    layout->addWidget(label(request.text, card));
    layout->addWidget(label(request.source, card));
    for (int index = 0; index < request.items.size(); ++index) {
        auto* button = new components::Button(request.items[index].title, card);
        button->setProperty("windowMessageAction", index);
        connect(button, &QPushButton::clicked, this, [this, request, index] { choose(request.owner, request.id, index); });
        layout->addWidget(button);
    }
    views_.append({request.owner, request.id, card, false});
    notificationLayout_->insertWidget(0, card);
    card->show();
    positionNotifications();
}
void WindowMessageService::showModal(const extensions::WindowMessageRequest& request) {
    if (!owner_) { choose(request.owner, request.id, {}); return; }
    auto* dialog = new QDialog(owner_);
    // Reuse the common dialog shell and its existing feature styles.
    dialog->setObjectName(QStringLiteral("confirmationDialog"));
    dialog->setWindowTitle(title(request.severity));
    dialog->setProperty("windowMessageOwner", request.owner);
    dialog->setProperty("windowMessageId", QString::number(request.id));
    auto* outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);
    outer->addWidget(new DialogHeader(title(request.severity), *dialog, dialog));
    auto* body = new QWidget(dialog);
    body->setObjectName(QStringLiteral("confirmationBody"));
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(16, 16, 16, 14);
    layout->setSpacing(18);
    layout->addWidget(label(request.text, body));
    if (request.options.detail) layout->addWidget(label(*request.options.detail, body));
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    std::optional<int> cancel;
    int regularCount = 0;
    QPushButton* primary = nullptr;
    for (int index = 0; index < request.items.size(); ++index) {
        if (request.items[index].isCloseAffordance.value_or(false)) { cancel = index; continue; }
        auto* button = new components::Button(request.items[index].title, body);
        button->setProperty("windowMessageAction", index);
        if (!primary) primary = button;
        ++regularCount;
        buttons->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, request, index] { choose(request.owner, request.id, index); });
    }
    auto* cancelButton = new components::Button(cancel ? request.items[*cancel].title : regularCount ? tr("Cancel") : tr("OK"), body);
    cancelButton->setObjectName(QStringLiteral("windowMessageCancel"));
    buttons->addWidget(cancelButton);
    connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog, &QDialog::rejected, this, [this, request, cancel] { choose(request.owner, request.id, cancel); });
    if (!primary) primary = cancelButton;
    DialogChrome::markPrimary(primary);
    primary->setDefault(true);
    layout->addLayout(buttons);
    outer->addWidget(body);
    DialogChrome::configure(*dialog);
    DialogChrome::fitToOwner(*dialog, QSize(440, dialog->sizeHint().height()), QSize(320, 160));
    views_.append({request.owner, request.id, dialog, true});
    dialog->open(); // Asynchronous: keep IPC and host shutdown processing alive.
}
void WindowMessageService::dismiss(const QString& owner, quint64 id) {
    for (auto it = views_.begin(); it != views_.end();) {
        if (it->owner == owner && it->id == id) {
            if (it->widget) {
                if (!it->modal && notificationLayout_) notificationLayout_->removeWidget(it->widget);
                it->widget->hide();
                it->widget->deleteLater();
            }
            it = views_.erase(it);
        } else ++it;
    }
    positionNotifications();
    if (modalQueue_) modalQueue_->release(*this, modalKey(owner, id));
}
void WindowMessageService::choose(const QString& owner, quint64 id, std::optional<int> selection) {
    if (messages_) messages_->select(owner, id, selection);
}
void WindowMessageService::positionNotifications() {
    if (!owner_ || !notifications_) return;
    const int width = qMin(420, qMax(1, owner_->width() - 24));
    qint64 height = 0;
    int count = 0;
    for (int index = 0; index < notificationLayout_->count(); ++index) {
        const auto* widget = notificationLayout_->itemAt(index)->widget();
        if (!widget || widget->isHidden()) continue;
        const auto* layout = widget->layout();
        height += layout && layout->hasHeightForWidth() ? layout->heightForWidth(width) : widget->sizeHint().height();
        ++count;
    }
    if (count == 0) { notifications_->hide(); return; }
    height += (count - 1) * notificationLayout_->spacing();
    const auto boundedHeight = static_cast<int>(qBound(qint64(1), height,
        static_cast<qint64>(qMax(1, owner_->height() - 80))));
    notifications_->resize(width, boundedHeight);
    notifications_->move(qMax(0, owner_->width() - width - 12), qMax(0, owner_->height() - boundedHeight - 36));
    notifications_->show();
    notifications_->raise();
}
bool WindowMessageService::eventFilter(QObject* watched, QEvent* event) {
    if (watched == owner_ && event->type() == QEvent::Resize) positionNotifications();
    return QObject::eventFilter(watched, event);
}
} // namespace litecode::ui
