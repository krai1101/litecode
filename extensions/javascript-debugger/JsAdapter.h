#pragma once
#include "debug/DapProtocol.h"
#include "debug/DapSocketConnection.h"
#include "extensions/DebugApi.h"
#include <QHash>
#include <QProcess>
#include <QTimer>

namespace litecode::javascript {
class JsAdapter final : public QObject, public extensions::DebugAdapter {
  public:
    JsAdapter(QString node, QString entryPoint);
    extensions::Event<QJsonObject> onDidSendMessage() const override { return output_.event(); }
    void handleMessage(const QJsonObject& message) override;
    void dispose() override;

  private:
    struct Connection {
        debug::DapSocketConnection socket;
        debug::DapDecoder decoder;
        QHash<int, int> requests;
        int sequence{};
        bool connected{};
    };

    void connectPeer(Connection& peer);
    void receive(Connection& peer, QJsonObject message);
    void send(Connection& peer, QJsonObject message, bool forwardResponse = true);
    void reply(const QJsonObject& request, bool success, const QString& message = {});
    void fail(const QString& message);

    Connection root_, child_;
    QProcess server_;
    QTimer startup_;
    extensions::EventEmitter<QJsonObject> output_;
    QHash<int, QPair<Connection*, int>> reverseRequests_;
    QList<QJsonObject> pending_, breakpointSettings_;
    QJsonObject initialize_, childLaunch_;
    QByteArray startupOutput_;
    QString node_, entryPoint_;
    int port_{}, sequence_{}, childLaunchSequence_{};
    bool starting_{}, childReady_{}, disposed_{}, ending_{};
};
} // namespace litecode::javascript
