#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <functional>
namespace litecode::extensions {
class WorkbenchUriHandlers final : public QObject {
    Q_OBJECT
  public:
    using Send = std::function<void(const QJsonObject&)>;
    explicit WorkbenchUriHandlers(QObject* parent = nullptr);
    bool accept(const QString& owner, const QJsonObject& message, Send send);
    bool dispatch(const QString& owner, const QUrl& uri);
    void removeOwner(const QString& owner);
  signals:
    void activationRequested(const QString& owner);
    void dispatched(const QString& owner);
    void error(const QString& owner, const QString& text);

  private:
    struct Handler {
        QString owner;
        quint64 token{};
        Send send;
    };
    struct Buffered {
        QUrl uri;
        qint64 timestamp{};
    };
    bool send(const Handler& handler, const QUrl& uri);
    void collect();
    QHash<QString, Handler> handlers_;
    QHash<QString, QVector<Buffered>> pending_;
    QElapsedTimer clock_;
    QTimer collectTimer_;
};
} // namespace litecode::extensions
