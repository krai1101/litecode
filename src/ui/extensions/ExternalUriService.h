#pragma once
#include "extensions/ExtensionManager.h"
#include <QPointer>
class QWidget;
class QDialog;
namespace litecode::ui {
class ModalDialogQueue;
class ExternalUriService final : public QObject {
    Q_OBJECT
  public:
    ExternalUriService(extensions::ExtensionManager& manager, QWidget* window, QObject* parent);
    ~ExternalUriService() override;
    bool handleIncoming(const QUrl& uri);
  signals:
    void workspaceRequested(const QUrl& uri);
    void error(const QString& message);
  private:
    void perform(const extensions::ExternalUriRequest& request);
    QPointer<extensions::ExtensionManager> manager_;
    QPointer<QWidget> window_;
    QPointer<ModalDialogQueue> modalQueue_;
    QPointer<QDialog> dialog_;
    quint64 nextDialog_{};
};
}
