#pragma once

#include <QHash>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <functional>

namespace litecode::codex {

// Extension-owned JSONL connection. Never linked into the workbench.
class AppServer final : public QObject {
    Q_OBJECT
  public:
    using Reply = std::function<void(const QJsonObject&, const QString&)>;
    explicit AppServer(QObject* parent = nullptr);
    ~AppServer() override;
    void start(const QString& executable = {});
    void stop();
    void reconnect();
    bool ready() const { return ready_; }
    qint64 request(const QString& method, const QJsonObject& params, Reply reply);
    void respond(const QJsonValue& id, const QJsonObject& result);
    void reject(const QJsonValue& id, const QString& reason);
    void ingest(const QByteArray& bytes);
  signals:
    void connected();
    void disconnected(const QString& reason);
    void notification(const QString& method, const QJsonObject& params);
    void interaction(const QJsonValue& id, const QString& method, const QJsonObject& params);

  private:
    struct Pending {
        Reply reply;
        qint64 expires;
    };
    void send(const QJsonObject& message);
    void flush();
    void fail(const QString& reason);
    QProcess* process_;
    QTimer deadlines_;
    QHash<qint64, Pending> pending_;
    QByteArray input_, output_;
    qint64 nextId_{1};
    bool ready_{false};
    bool reconnecting_{false};
    QString executable_;
};
} // namespace litecode::codex
