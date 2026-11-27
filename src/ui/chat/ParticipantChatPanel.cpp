#include "ui/chat/ParticipantChatPanel.h"
#include "extensions/ExtensionManager.h"
#include "ui/chat/ParticipantResponseView.h"
#include "ui/components/Controls.h"
#include "ui/components/SidebarView.h"
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QUuid>
#include <QVBoxLayout>

namespace litecode::ui::chat {
ParticipantChatPanel::ParticipantChatPanel(extensions::ExtensionManager* manager, QWidget* parent)
    : QWidget(parent), manager_(manager),
      session_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      participants_(new components::ComboBox(this)), commands_(new components::ComboBox(this)),
      input_(new components::Input(this)),
      send_(new components::Button(tr("Send"), this, components::ButtonVariant::Primary)),
      stop_(new components::Button(tr("Stop"), this)), status_(new QLabel(this)) {
    setObjectName("participantChatPanel");
    participants_->setObjectName("chatParticipantSelector");
    commands_->setObjectName("chatCommandSelector");
    input_->setObjectName("chatPromptInput");
    send_->setObjectName("chatSendButton");
    stop_->setObjectName("chatStopButton");
    status_->setObjectName("chatStatusLabel");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(components::SidebarView::controlMargins());
    auto* toolbar = new QHBoxLayout;
    participants_->setAccessibleName(tr("Chat participant"));
    commands_->setAccessibleName(tr("Participant command"));
    toolbar->addWidget(participants_, 1);
    toolbar->addWidget(commands_);
    auto* clear = new components::Button(tr("New Chat"), this);
    clear->setObjectName("chatNewSessionButton");
    toolbar->addWidget(clear);
    layout->addLayout(toolbar);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    turns_ = new QVBoxLayout(body);
    turns_->setContentsMargins(0, 0, 0, 0);
    turns_->addStretch();
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    input_->setPlaceholderText(tr("Ask a question"));
    input_->setAccessibleName(tr("Chat prompt"));
    layout->addWidget(input_);
    auto* actions = new QHBoxLayout;
    actions->addStretch();
    actions->addWidget(stop_);
    actions->addWidget(send_);
    layout->addLayout(actions);
    connect(clear, &QPushButton::clicked, this, &ParticipantChatPanel::clearSession);
    connect(send_, &QPushButton::clicked, this, [this] { submit(input_->text()); });
    connect(input_, &QLineEdit::returnPressed, this, [this] { submit(input_->text()); });
    connect(input_, &QLineEdit::textChanged, this, [this] { updateActions(); });
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (manager_ && active_)
            manager_->cancelChat(active_);
    });
    connect(participants_, &QComboBox::currentIndexChanged, this,
            &ParticipantChatPanel::refreshCommands);
    connect(manager_, &extensions::ExtensionManager::changed, this,
            &ParticipantChatPanel::refreshParticipants);
    connect(
        manager_, &extensions::ExtensionManager::chatResponsePart, this,
        [this](quint64 id, quint64 sequence, const QJsonObject& part) {
            const auto view = responses_.value(id);
            if (!view || id != active_)
                return;
            if (sequence != sequences_.value(id)) {
                manager_->cancelChat(id);
                status_->setText(tr("Chat response arrived out of order."));
                return;
            }
            sequences_[id] = sequence + 1;
            view->appendPart(part);
        },
        Qt::QueuedConnection);
    connect(
        manager_, &extensions::ExtensionManager::chatRequestFinished, this,
        [this](quint64 id, const QJsonObject& result, const QString& error) {
            const auto view = responses_.value(id);
            if (!view)
                return;
            view->finish(result, error);
            if (id == active_) {
                active_ = 0;
                updateActions();
            }
            sequences_.remove(id);
            if (!error.isEmpty())
                return;
            auto* feedback = new QWidget(view);
            auto* row = new QHBoxLayout(feedback);
            row->setContentsMargins(0, 0, 0, 0);
            for (const auto kind : {extensions::ChatResultFeedbackKind::Helpful,
                                    extensions::ChatResultFeedbackKind::Unhelpful}) {
                auto* button = new components::Button(
                    kind == extensions::ChatResultFeedbackKind::Helpful ? tr("Helpful")
                                                                        : tr("Unhelpful"),
                    feedback, components::ButtonVariant::Ghost);
                button->setObjectName(kind == extensions::ChatResultFeedbackKind::Helpful
                                          ? "chatFeedbackHelpful"
                                          : "chatFeedbackUnhelpful");
                row->addWidget(button);
                connect(button, &QPushButton::clicked, this, [this, id, kind] {
                    if (!manager_ || !manager_->sendChatFeedback(id, kind))
                        status_->setText(
                            tr("The participant is no longer available for feedback."));
                });
            }
            row->addStretch();
            view->layout()->addWidget(feedback);
        },
        Qt::QueuedConnection);
    connect(
        manager_, &extensions::ExtensionManager::chatFollowupsReady, this,
        [this](quint64 id, const QJsonArray& followups, const QString& error) {
            const auto view = responses_.value(id);
            if (!view)
                return;
            if (!error.isEmpty()) {
                status_->setText(error);
                return;
            }
            for (const auto& value : followups) {
                const auto followup = value.toObject();
                auto* button = new components::Button(
                    followup.value("label").toString(followup.value("prompt").toString()), view);
                button->setObjectName("chatFollowupButton");
                view->layout()->addWidget(button);
                const auto participant = followup.value("participant")
                                             .toString(view->property("participantId").toString());
                connect(button, &QPushButton::clicked, this, [this, followup, participant] {
                    submit(followup.value("prompt").toString(), participant,
                           followup.value("command").toString());
                });
            }
        },
        Qt::QueuedConnection);
    refreshParticipants();
}

ParticipantChatPanel::~ParticipantChatPanel() {
    if (manager_) {
        disconnect(manager_, nullptr, this, nullptr);
        manager_->closeChatSession(session_);
    }
}

void ParticipantChatPanel::refreshParticipants() {
    const auto selected = participants_->currentData().toString();
    QSignalBlocker blocker(participants_);
    participants_->clear();
    for (const auto& participant : manager_->chatParticipantContributions()) {
        participants_->addItem(participant.fullName.isEmpty() ? participant.name
                                                              : participant.fullName,
                               participant.id);
        participants_->setItemData(participants_->count() - 1, participant.description,
                                   Qt::ToolTipRole);
    }
    auto index = participants_->findData(selected);
    participants_->setCurrentIndex(index >= 0 ? index : (participants_->count() ? 0 : -1));
    // Rebuild command choices even when the selected participant ID is unchanged.
    blocker.unblock();
    refreshCommands();
    updateActions();
}

void ParticipantChatPanel::refreshCommands() {
    const auto selected = commands_->currentData().toString();
    commands_->clear();
    commands_->addItem(tr("No command"), QString{});
    if (manager_)
        for (const auto& participant : manager_->chatParticipantContributions()) {
            if (participant.id != participants_->currentData().toString())
                continue;
            for (const auto& value : participant.commands) {
                const auto command = value.toObject();
                commands_->addItem('/' + command.value("name").toString(),
                                   command.value("name").toString());
                commands_->setItemData(commands_->count() - 1,
                                       command.value("description").toString(), Qt::ToolTipRole);
            }
        }
    const auto index = commands_->findData(selected);
    commands_->setCurrentIndex(index >= 0 ? index : 0);
    updateActions();
}

void ParticipantChatPanel::updateActions() {
    send_->setEnabled(manager_ && !active_ && participants_->currentIndex() >= 0 &&
                      !input_->text().trimmed().isEmpty());
    stop_->setEnabled(manager_ && active_ != 0);
    participants_->setEnabled(!active_);
    commands_->setEnabled(!active_);
    if (participants_->count() == 0)
        status_->setText(
            tr("Enable an extension that contributes a chat participant to start chatting."));
}

void ParticipantChatPanel::submit(const QString& prompt, const QString& target,
                                  const QString& followupCommand) {
    if (!manager_ || active_ || prompt.trimmed().isEmpty())
        return;
    const auto participant = target.isEmpty() ? participants_->currentData().toString() : target;
    if (participant.isEmpty())
        return;
    QJsonObject request{
        {"prompt", prompt}, {"references", QJsonArray{}}, {"toolReferences", QJsonArray{}}};
    const auto command = target.isEmpty() ? commands_->currentData().toString() : followupCommand;
    if (!command.isEmpty())
        request.insert("command", command);
    // Signal deliveries are queued so the view records the ID before responses.
    const auto id = manager_->requestChat(session_, participant, request);
    if (!id) {
        status_->setText(tr("This chat participant is unavailable."));
        return;
    }
    auto* question = new QLabel(prompt, this);
    question->setTextFormat(Qt::PlainText);
    question->setWordWrap(true);
    question->setTextInteractionFlags(Qt::TextSelectableByMouse);
    turns_->insertWidget(turns_->count() - 1, question);
    auto* view = new ParticipantResponseView(manager_, this);
    view->setProperty("participantId", participant);
    turns_->insertWidget(turns_->count() - 1, view);
    responses_.insert(id, view);
    sequences_.insert(id, 0);
    connect(view, &ParticipantResponseView::resourceActivated, this,
            &ParticipantChatPanel::resourceActivated);
    active_ = id;
    input_->clear();
    status_->clear();
    updateActions();
}

void ParticipantChatPanel::clearSession() {
    if (manager_)
        manager_->closeChatSession(session_);
    session_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    active_ = 0;
    responses_.clear();
    sequences_.clear();
    while (turns_->count() > 1) {
        auto* item = turns_->takeAt(0);
        delete item->widget();
        delete item;
    }
    status_->clear();
    updateActions();
}
} // namespace litecode::ui::chat
