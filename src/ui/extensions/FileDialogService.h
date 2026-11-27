#pragma once
#include "extensions/WorkbenchDialogs.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <functional>

class QFileDialog;
class QWidget;
namespace litecode::ui {
class ModalDialogQueue;
class FileDialogService final : public QObject {
  public:
    FileDialogService(extensions::WorkbenchDialogs& dialogs, QWidget* owner,
                      std::function<QUrl()> defaultUri, QObject* parent);
    ~FileDialogService() override;

  private:
    void show(const extensions::FileDialogRequest& request);
    void dismiss(const QString& owner, quint64 id);
    QPointer<extensions::WorkbenchDialogs> dialogs_;
    QPointer<QWidget> owner_;
    std::function<QUrl()> defaultUri_;
    QPointer<ModalDialogQueue> modalQueue_;
    QHash<QString, QByteArray> dialogKeys_;
    QPointer<QFileDialog> active_;
    QString activeOwner_;
    quint64 activeId_{};
};
} // namespace litecode::ui
