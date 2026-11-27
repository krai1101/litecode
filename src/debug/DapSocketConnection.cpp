#include "debug/DapSocketConnection.h"
#include <QLocalSocket>
#include <QPointer>
#include <QTcpSocket>
#include <utility>

namespace litecode::debug {
DapSocketConnection::DapSocketConnection(QObject* parent) : QObject(parent) {}
DapSocketConnection::~DapSocketConnection() { close(); }
void DapSocketConnection::connectTcp(const QString& host, quint16 port) {
    auto* socket = new QTcpSocket(this);
    device_ = socket;
    connect(socket, &QTcpSocket::connected, this, &DapSocketConnection::connected);
    connect(socket, &QTcpSocket::readyRead, this, &DapSocketConnection::drain);
    connect(socket, &QTcpSocket::disconnected, this, [this] {
        const QPointer<DapSocketConnection> alive(this);
        drain(); if (alive && device_) emit closed();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        // RemoteHostClosedError is followed by disconnected; drain final DAP
        // bytes there before rejecting requests which the peer did not answer.
        if (error != QAbstractSocket::RemoteHostClosedError) emit errorOccurred(socket->errorString());
    });
    socket->connectToHost(host, port);
}
void DapSocketConnection::connectLocal(const QString& path) {
    auto* socket = new QLocalSocket(this);
    device_ = socket;
    connect(socket, &QLocalSocket::connected, this, &DapSocketConnection::connected);
    connect(socket, &QLocalSocket::readyRead, this, &DapSocketConnection::drain);
    connect(socket, &QLocalSocket::disconnected, this, [this] {
        const QPointer<DapSocketConnection> alive(this);
        drain(); if (alive && device_) emit closed();
    });
    connect(socket, &QLocalSocket::errorOccurred, this, [this, socket](QLocalSocket::LocalSocketError error) {
        if (error != QLocalSocket::PeerClosedError) emit errorOccurred(socket->errorString());
    });
    socket->connectToServer(path, QIODevice::ReadWrite);
}
void DapSocketConnection::drain() {
    const QPointer<DapSocketConnection> alive(this);
    while (device_ && device_->bytesAvailable() > 0) {
        emit bytesReceived(device_->read(65536));
        if (!alive) return;
    }
}
qint64 DapSocketConnection::queuedBytes() const { return device_ ? device_->bytesToWrite() : 0; }
bool DapSocketConnection::write(const QByteArray& bytes) { return device_ && device_->write(bytes) == bytes.size(); }
void DapSocketConnection::close() {
    auto* device = std::exchange(device_, nullptr);
    if (!device) return;
    device->disconnect(this);
    if (auto* tcp = qobject_cast<QTcpSocket*>(device)) tcp->abort();
    else if (auto* local = qobject_cast<QLocalSocket*>(device)) local->abort();
    device->deleteLater();
}
} // namespace litecode::debug
