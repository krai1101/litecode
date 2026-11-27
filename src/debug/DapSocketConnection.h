#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

class QIODevice;
namespace litecode::debug {
// Private Qt stream bridge. A socket adapter is owned by its provider, not by
// the editor: closing this connection must never kill that server process.
class DapSocketConnection final : public QObject {
    Q_OBJECT
  public:
    explicit DapSocketConnection(QObject* parent = nullptr);
    ~DapSocketConnection() override;
    void connectTcp(const QString& host, quint16 port);
    void connectLocal(const QString& path);
    qint64 queuedBytes() const;
    bool write(const QByteArray& bytes);
    void close();
  signals:
    void connected();
    void bytesReceived(const QByteArray& bytes);
    void closed();
    void errorOccurred(const QString& message);

  private:
    void drain();
    QIODevice* device_{};
};
} // namespace litecode::debug
