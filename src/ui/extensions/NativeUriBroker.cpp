#include "ui/extensions/NativeUriBroker.h"
#include "extensions/UriWire.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace litecode::ui {
namespace {
constexpr qsizetype MaxMessage = 64 * 1024;
QString directoryFor(const QString& profile) {
    const auto key =
        QCryptographicHash::hash(profile.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           "/uri-routing/" + QString::fromLatin1(key);
}
bool productUri(const QUrl& uri) {
    return uri.isValid() &&
           uri.scheme().compare(extensions::wire::AppUriScheme, Qt::CaseInsensitive) == 0;
}
} // namespace
struct NativeUriBroker::State {
    QString directory, endpoint, record;
    QLocalServer server;
    std::function<bool(const QUrl&)> receive;
    qint64 lastActivation{};
};
NativeUriBroker::NativeUriBroker(QString profile, QObject* parent)
    : QObject(parent), state_(std::make_unique<State>()) {
    state_->directory = directoryFor(profile);
    state_->endpoint = "litecode-uri-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    state_->record = state_->directory + '/' + state_->endpoint + ".json";
}
NativeUriBroker::~NativeUriBroker() {
    state_->server.close();
    QFile::remove(state_->record);
}
bool NativeUriBroker::listen(std::function<bool(const QUrl&)> receive) {
    if (state_->server.isListening())
        return false;
    if (!QDir().mkpath(state_->directory))
        return false;
    state_->server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!state_->server.listen(state_->endpoint))
        return false;
    state_->receive = std::move(receive);
    connect(&state_->server, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = state_->server.nextPendingConnection()) {
            socket->setParent(this);
            socket->setReadBufferSize(MaxMessage + 1);
            auto* timeout = new QTimer(socket);
            timeout->setSingleShot(true);
            connect(timeout, &QTimer::timeout, socket, &QLocalSocket::abort);
            timeout->start(2000);
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer, timeout] {
                buffer->append(socket->readAll());
                if (buffer->size() > MaxMessage) {
                    socket->abort();
                    return;
                }
                const auto newline = buffer->indexOf('\n');
                if (newline < 0)
                    return;
                // Exactly one bounded request per connection; no URI diagnostics.
                const auto document = QJsonDocument::fromJson(buffer->left(newline));
                const auto uri = extensions::wire::decodeUri(document.object().value("uri"));
                if (!document.isObject() || !uri || !productUri(*uri)) {
                    socket->abort();
                    return;
                }
                timeout->stop();
                socket->disconnect(this);
                const auto handler = state_->receive;
                const QPointer<QLocalSocket> guarded(socket);
                bool accepted = false;
                try {
                    accepted = handler && handler(*uri);
                } catch (...) { /* No secrets in diagnostics. */
                }
                // Acknowledge queue acceptance, not extension completion or
                // user consent. Routing failures must not silently eat links.
                if (guarded) {
                    guarded->write(accepted ? "accepted\n" : "rejected\n");
                    guarded->disconnectFromServer();
                }
            });
        }
    });
    activated();
    if (!QFileInfo::exists(state_->record)) {
        state_->server.close();
        return false;
    }
    return true;
}
void NativeUriBroker::activated() {
    if (!state_->server.isListening())
        return;
    state_->lastActivation =
        std::max(QDateTime::currentMSecsSinceEpoch(), state_->lastActivation + 1);
    QSaveFile record(state_->record);
    if (!record.open(QIODevice::WriteOnly))
        return;
    record.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto data =
        QJsonDocument(QJsonObject{{"endpoint", state_->endpoint},
                                  {"active", QString::number(state_->lastActivation)}})
            .toJson(QJsonDocument::Compact);
    if (record.write(data) == data.size())
        record.commit();
}
qsizetype NativeUriBroker::forward(const QString& profile, const QList<QUrl>& uris) {
    if (uris.isEmpty())
        return 0;
    QElapsedTimer deadline;
    deadline.start();
    constexpr qint64 ForwardBudget = 3000;
    QList<QByteArray> messages;
    for (const auto& uri : uris) {
        if (!productUri(uri))
            return 0;
        auto message = QJsonDocument(QJsonObject{{"uri", extensions::wire::encodeUri(uri)}})
                           .toJson(QJsonDocument::Compact) +
                       '\n';
        if (message.size() > MaxMessage)
            return 0;
        messages.append(std::move(message));
    }
    struct Peer {
        QString endpoint;
        qint64 active;
    };
    QList<Peer> peers;
    QDirIterator entries(directoryFor(profile), {"litecode-uri-*.json"},
                         QDir::Files | QDir::NoSymLinks);
    while (entries.hasNext()) {
        if (deadline.elapsed() >= ForwardBudget)
            return 0;
        entries.next();
        const auto entry = entries.fileInfo();
        if (entry.size() > 1024)
            continue;
        QFile record(entry.absoluteFilePath());
        if (!record.open(QIODevice::ReadOnly))
            continue;
        const auto object = QJsonDocument::fromJson(record.read(1025)).object();
        const auto endpoint = object.value("endpoint").toString();
        bool ok = false;
        const auto active = object.value("active").toString().toLongLong(&ok);
        // Endpoint metadata cannot redirect us to arbitrary local IPC services.
        if (!ok || endpoint != entry.completeBaseName() ||
            QUuid(endpoint.mid(QString("litecode-uri-").size())).isNull())
            continue;
        peers.append({endpoint, active});
    }
    std::stable_sort(peers.begin(), peers.end(),
                     [](const Peer& a, const Peer& b) { return a.active > b.active; });
    // Stale records from a crashed process are harmless: only a live endpoint
    // with an explicit receipt is used. No other process's record is deleted.
    for (const auto& peer : peers) {
        if (deadline.elapsed() >= ForwardBudget)
            return 0;
        qsizetype delivered = 0;
        for (const auto& message : messages) {
            if (deadline.elapsed() >= ForwardBudget)
                return delivered;
            QLocalSocket socket;
            socket.connectToServer(peer.endpoint);
            auto remaining = ForwardBudget - deadline.elapsed();
            if (remaining <= 0 ||
                !socket.waitForConnected(static_cast<int>(std::min(qint64(150), remaining))))
                break;
            if (socket.write(message) != message.size())
                break;
            remaining = ForwardBudget - deadline.elapsed();
            if (remaining <= 0 ||
                (socket.bytesToWrite() &&
                 !socket.waitForBytesWritten(static_cast<int>(std::min(qint64(500), remaining)))))
                break;
            QByteArray reply;
            while (!reply.contains('\n') && reply.size() < 32) {
                const auto replyBudget = ForwardBudget - deadline.elapsed();
                if (replyBudget <= 0)
                    break;
                if (!socket.bytesAvailable() &&
                    !socket.waitForReadyRead(static_cast<int>(std::min(qint64(1000), replyBudget))))
                    break;
                reply.append(socket.read(32 - reply.size()));
            }
            if (reply != "accepted\n")
                break;
            ++delivered;
        }
        // Never replay a partially delivered batch into another window.
        if (delivered)
            return delivered;
    }
    return 0;
}
} // namespace litecode::ui
