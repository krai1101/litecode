#pragma once
#include "debug/TaskConfiguration.h"
#include <QProcess>

namespace litecode::debug {
// Run user-configured tasks in dependency order before launching a program.
class TaskRunner final : public QObject {
    Q_OBJECT
  public:
    explicit TaskRunner(QObject* parent = nullptr);
    bool start(const QString& workspace, const QString& label, const VariableContext& context);
    void stop();
    bool active() const { return active_; }
  signals:
    void outputReceived(const QString& text);
    void finished(bool success, const QString& error);
  private:
    void runNext();
    void finish(bool success, const QString& error);
    QProcess process_;
    QVector<TaskDefinition> graph_;
    VariableContext context_;
    QString currentLabel_;
    int nextIndex_{};
    bool active_{};
};
} // namespace litecode::debug
