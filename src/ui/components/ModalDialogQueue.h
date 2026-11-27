#pragma once
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <functional>
#include <optional>

class QWidget;
namespace litecode::ui {
// One modal queue per workbench window, shared by different dialog services.
// This is UI infrastructure, never part of the extension SDK.
class ModalDialogQueue final : public QObject {
    Q_OBJECT
  public:
    static ModalDialogQueue& forWindow(QWidget& window);
    void enqueue(QObject& service, const QString& key, std::function<void()> show);
    void release(QObject& service, const QString& key);
  private:
    explicit ModalDialogQueue(QObject* parent);
    void schedule();
    void next();
    void removeService(QObject* service);
    struct Task {
        QObject* identity{};
        QPointer<QObject> service;
        QString key;
        std::function<void()> show;
    };
    QQueue<Task> pending_;
    std::optional<Task> active_;
    QSet<QObject*> observed_;
    bool scheduled_{};
};
} // namespace litecode::ui
