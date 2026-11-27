#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QUrl>
#include <QVector>
#include <functional>
class QWidget;
namespace litecode::ui {
class NativeUriDispatcher final : public QObject {
    Q_OBJECT
  public:
    static NativeUriDispatcher& forApplication();
    void registerWindow(QWidget& window, QObject& lifetime, std::function<bool(const QUrl&)> handler);
  signals:
    void windowActivated();
    void routingError(const QString& message);
  public slots:
    bool dispatchUri(const QUrl& uri);
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
  private:
    explicit NativeUriDispatcher(QObject* parent);
    ~NativeUriDispatcher() override;
    struct Receiver { QPointer<QWidget> window; QPointer<QObject> lifetime; std::function<bool(const QUrl&)> handler; quint64 order{}; };
    QVector<Receiver> receivers_;
    struct Pending { QUrl uri; qint64 received; };
    QVector<Pending> pending_;
    QElapsedTimer clock_;
    quint64 nextOrder_{};
};
}
