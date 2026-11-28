#pragma once
#include "debug/DapProtocol.h"
#include <QProcessEnvironment>
#include <functional>

class QProcess;
namespace litecode::debug {
class DapSocketConnection;
struct AdapterProcess {
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
    int startupTimeoutMs{10000};
};
struct AdapterServer {
    quint16 port{};
    QString host{QStringLiteral("127.0.0.1")};
    int startupTimeoutMs{10000};
};
struct AdapterNamedPipeServer {
    QString path;
    int startupTimeoutMs{10000};
};
// One DAP stream per session, from an owned process or a provider-owned server.
// Standard output is exclusively DAP; stderr is separate diagnostic output.
class DapTransport final : public QObject {
    Q_OBJECT
  public:
    enum class State { Idle, Starting, Running, Stopping };
    Q_ENUM(State)
    explicit DapTransport(QObject* parent = nullptr);
    ~DapTransport() override;
    State state() const { return state_; }
    bool start(const AdapterProcess& configuration);
    bool startServer(const AdapterServer& configuration);
    bool startNamedPipeServer(const AdapterNamedPipeServer& configuration);
    void stop();
    void stopAfterDisconnect();
    int request(const QString& command, const QJsonObject& arguments = {}, int timeoutMs = 10000);
    int requestValue(const QString& command, const QJsonValue& arguments, int timeoutMs = 10000);
    bool respond(const QJsonObject& request, bool success, const QJsonObject& body = {},
                 const QString& message = {});
  signals:
    void stateChanged(litecode::debug::DapTransport::State state);
    void started();
    void stopped(int exitCode, bool crashed);
    void errorOccurred(const QString& message);
    void diagnosticOutput(const QByteArray& bytes);
    void responseReceived(int requestSequence, const QString& command, bool success,
                          const QJsonObject& body, const QString& message);
    void eventReceived(const QString& event, const QJsonObject& body);
    void responseValueReceived(int requestSequence, const QString& command, bool success,
                               const QJsonValue& body, const QString& message);
    void eventValueReceived(const QString& event, const QJsonValue& body);
    void reverseRequestReceived(const QJsonObject& request);
    void messageSent(const QJsonObject& message);
    void messageReceived(const QJsonObject& message);

  private:
    void stop(bool allowNaturalExit);
    void setState(State state);
    void drain(QProcess* process, quint64 generation);
    void finish(QProcess* process, int exitCode, bool crashed);
    bool startSocket(int startupTimeoutMs,
                     const std::function<void(DapSocketConnection&)>& connect);
    void finishSocket(DapSocketConnection* connection, bool unexpected);
    void abort(const QString& message);
    DapChannel channel_;
    QProcess* process_{};
    DapSocketConnection* connection_{};
    QTimer startup_;
    QTimer shutdown_;
    State state_{State::Idle};
    bool finishing_{};
};
} // namespace litecode::debug
Q_DECLARE_METATYPE(litecode::debug::DapTransport::State)
