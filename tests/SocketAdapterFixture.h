#pragma once
#include "debug/DapProtocol.h"
#include <QIODevice>
#include <QJsonArray>
#include <QPointer>
#include <memory>

// Test-only DAP peer attached to an actual TCP/local socket. It neither mocks
// the client's channel nor bypasses byte framing or the session handshake.
class SocketAdapterFixture final : public QObject {
  public:
    explicit SocketAdapterFixture(QIODevice* peer, QObject* parent = nullptr)
        : QObject(parent), peer_(peer) {
        connect(peer, &QIODevice::readyRead, this, [this] {
            for (const auto& request : decoder_.feed(peer_->readAll())) handle(request);
        });
    }
    QStringList commands;
    QJsonObject reverseResponse;
    QJsonObject launchArguments;
    QJsonObject disconnectArguments;
    void send(QJsonObject message) {
        if (!peer_) return;
        message.insert("seq", ++sequence_);
        const auto bytes = litecode::debug::DapDecoder::encode(message);
        // Force the parser to handle split headers and payloads.
        peer_->write(bytes.left(7)); peer_->write(bytes.mid(7));
    }
  private:
    void reply(const QJsonObject& request, const QJsonObject& body = {}) {
        send({{"type", "response"}, {"request_seq", request.value("seq")},
              {"command", request.value("command")}, {"success", true}, {"body", body}});
    }
    void handle(const QJsonObject& request) {
        if (request.value("type") == QJsonValue("response")) { reverseResponse = request; return; }
        const auto command = request.value("command").toString();
        commands.append(command);
        if (command == "hold") return;
        if (command == "malformed") { peer_->write("Content-Length: -1\r\n\r\n"); return; }
        if (command == "reverse") {
            send({{"type", "request"}, {"command", "runInTerminal"}, {"arguments", QJsonObject{{"kind", "integrated"}}}});
        } else if (command == "initialize") {
            reply(request, {{"supportsConfigurationDoneRequest", true}}); return;
        } else if (command == "launch" || command == "attach") {
            launch_ = request; launchArguments = request.value("arguments").toObject();
            send({{"type", "event"}, {"event", "initialized"}}); return;
        } else if (command == "configurationDone") {
            reply(request); reply(launch_); return;
        } else if (command == "setBreakpoints") {
            reply(request, {{"breakpoints", request.value("arguments").toObject().value("breakpoints")}}); return;
        } else if (command == "disconnect") {
            disconnectArguments = request.value("arguments").toObject(); reply(request); return;
        } else if (command == "pause" || command == "next") {
            reply(request); send({{"type", "event"}, {"event", "stopped"},
                {"body", QJsonObject{{"threadId", 1}, {"reason", "step"}}}}); return;
        } else if (command == "evaluate") {
            reply(request, {{"result", "42"}, {"variablesReference", 0}}); return;
        }
        reply(request, {{"echo", request.value("arguments")}});
    }
    QPointer<QIODevice> peer_;
    litecode::debug::DapDecoder decoder_;
    QJsonObject launch_;
    int sequence_{};
};
