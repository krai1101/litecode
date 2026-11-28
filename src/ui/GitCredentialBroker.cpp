#include "ui/GitCredentialBroker.h"
#include "ui/ConfirmationDialog.h"
#include "ui/components/QuickInputDialog.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocalSocket>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <memory>

namespace litecode::ui {
GitCredentialBroker::GitCredentialBroker(QWidget* owner, QObject* parent)
    : QObject(parent), owner_(owner), token_(QUuid::createUuid().toString(QUuid::Id128) +
                                             QUuid::createUuid().toString(QUuid::Id128)) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_.listen("LiteCodeGit-" + QUuid::createUuid().toString(QUuid::Id128)))
        return;
    auto* app = QCoreApplication::instance();
    app->setProperty("litecode.git.askpass.endpoint", server_.serverName());
    app->setProperty("litecode.git.askpass.token", token_);
    connect(&server_, &QLocalServer::newConnection, this, &GitCredentialBroker::acceptConnections);
}
GitCredentialBroker::~GitCredentialBroker() {
    auto* app = QCoreApplication::instance();
    if (app && app->property("litecode.git.askpass.token").toString() == token_) {
        app->setProperty("litecode.git.askpass.endpoint", QVariant{});
        app->setProperty("litecode.git.askpass.token", QVariant{});
    }
}
void GitCredentialBroker::acceptConnections() {
    while (auto* socket = server_.nextPendingConnection()) {
        auto bytes = std::make_shared<QByteArray>();
        auto* timer = new QTimer(socket);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, socket, &QLocalSocket::abort);
        timer->start(5000);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket, bytes, timer] {
            if (socket->property("handled").toBool())
                return;
            *bytes += socket->readAll();
            if (bytes->size() > 65536) {
                socket->abort();
                return;
            }
            if (!bytes->contains('\n'))
                return;
            socket->setProperty("handled", true);
            timer->stop();
            const auto message = QJsonDocument::fromJson(*bytes).object();
            bytes->fill('\0');
            bytes->clear();
            const auto operation = message.value("operation").toString();
            const auto request = message.value("prompt").toString();
            if (message.value("token").toString() != token_ || operation.size() != 32 ||
                !QCoreApplication::instance()
                     ->property(("litecode.git.active." + operation).toUtf8())
                     .toBool() ||
                request.isEmpty() || request.size() > 8192 || prompting_ || !owner_) {
                socket->abort();
                return;
            }
            prompt(socket, operation, request);
        });
    }
}
void GitCredentialBroker::prompt(QLocalSocket* socket, const QString& operation,
                                 const QString& request) {
    prompting_ = true;
    QPointer<QLocalSocket> connection(socket);
    // Treat arbitrary SSH questions as secret input unless it is a username or
    // explicit host authenticity question. Never silently accept a host key.
    const bool trust = request.contains("continue connecting", Qt::CaseInsensitive);
    std::unique_ptr<QDialog> dialog;
    if (trust) {
        dialog =
            std::make_unique<ConfirmationDialog>(tr("Git: SSH host authenticity"), request,
                                                 tr("Trust host"), QString{}, tr("Cancel"), owner_);
        if (auto* label = dialog->findChild<QLabel*>("confirmationMessage"))
            label->setTextFormat(Qt::PlainText);
    } else {
        auto input = std::make_unique<components::QuickInputDialog>(
            tr("Git authentication"), request, tr("Credential"), owner_);
        input->setObjectName("gitCredentialDialog");
        input->input()->setEchoMode(request.startsWith("Username", Qt::CaseInsensitive)
                                        ? QLineEdit::Normal
                                        : QLineEdit::Password);
        dialog = std::move(input);
    }
    connect(socket, &QLocalSocket::disconnected, dialog.get(), &QDialog::reject);
    QTimer lifetime;
    connect(&lifetime, &QTimer::timeout, dialog.get(), [&] {
        if (!QCoreApplication::instance()
                 ->property(("litecode.git.active." + operation).toUtf8())
                 .toBool())
            dialog->reject();
    });
    lifetime.start(100);
    const bool confirmed = dialog->exec() == QDialog::Accepted;
    const bool accepted =
        confirmed && (!trust || static_cast<ConfirmationDialog*>(dialog.get())->choice() ==
                                    ConfirmationChoice::Primary);
    QString value;
    if (accepted)
        value = trust ? "yes"
                      : static_cast<components::QuickInputDialog*>(dialog.get())->input()->text();
    if (connection && connection->state() == QLocalSocket::ConnectedState) {
        connection->write(QJsonDocument(QJsonObject{{"accepted", accepted}, {"value", value}})
                              .toJson(QJsonDocument::Compact) +
                          '\n');
        connection->flush();
        connection->disconnectFromServer();
    }
    value.fill(QChar('\0'));
    prompting_ = false;
}
} // namespace litecode::ui
