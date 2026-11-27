#pragma once
#include <QLocalServer>
#include <QObject>
#include <QPointer>
class QWidget;
class QLocalSocket;
namespace litecode::ui {
// Current-user-only IPC fallback. Git's configured credential helper remains first choice.
class GitCredentialBroker final : public QObject {
  public:
    explicit GitCredentialBroker(QWidget* owner, QObject* parent = nullptr);
    ~GitCredentialBroker() override;

  private:
    void acceptConnections();
    void prompt(QLocalSocket* socket, const QString& operation, const QString& request);
    QLocalServer server_;
    QPointer<QWidget> owner_;
    QString token_;
    bool prompting_{};
};
} // namespace litecode::ui
