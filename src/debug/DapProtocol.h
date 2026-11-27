#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace litecode::debug {
// Stateful, bounded UTF-8 Content-Length framing, independent of transport.
class DapDecoder final {
  public:
    static constexpr qsizetype MaxHeaderBytes = 8192;
    static constexpr qsizetype MaxPayloadBytes = 16 * 1024 * 1024;
    QVector<QJsonObject> feed(const QByteArray& bytes);
    QString error() const { return error_; }
    bool hasPartialMessage() const { return !buffer_.isEmpty() || expected_ >= 0; }
    void reset();
    static QByteArray encode(const QJsonObject& message);

  private:
    QByteArray buffer_;
    qsizetype expected_{-1};
    QString error_;
};

// Owns correlation/deadlines, not processes. A transport must tag inbound bytes
// with generation() so late events from a previous adapter cannot affect a session.
class DapChannel final : public QObject {
    Q_OBJECT
  public:
    explicit DapChannel(QObject* parent = nullptr);
    quint64 generation() const { return generation_; }
    int request(const QString& command, const QJsonObject& arguments = {}, int timeoutMs = 10000);
    int requestValue(const QString& command, const QJsonValue& arguments, int timeoutMs = 10000);
    bool respond(const QJsonObject& request, bool success, const QJsonObject& body = {},
                 const QString& message = {});
    void receive(const QByteArray& bytes, quint64 generation);
    void reset(const QString& reason = QStringLiteral("Debug transport closed."));
  signals:
    void writeRequested(const QByteArray& frame, quint64 generation);
    void messageSent(const QJsonObject& message);
    void messageReceived(const QJsonObject& message);
    void responseReceived(int requestSequence, const QString& command, bool success,
                          const QJsonObject& body, const QString& message);
    void eventReceived(const QString& event, const QJsonObject& body);
    void responseValueReceived(int requestSequence, const QString& command, bool success,
                               const QJsonValue& body, const QString& message);
    void eventValueReceived(const QString& event, const QJsonValue& body);
    void reverseRequestReceived(const QJsonObject& request);
    void protocolError(const QString& message);

  private:
    struct Pending {
        QString command;
        qint64 deadline{};
    };
    int nextSequence();
    void expire();
    void fail(const QString& message);
    DapDecoder decoder_;
    QHash<int, Pending> pending_;
    QElapsedTimer clock_;
    QTimer deadlines_;
    quint64 generation_{1};
    int sequence_{};
    bool resetting_{};
    bool failed_{};
};
} // namespace litecode::debug
