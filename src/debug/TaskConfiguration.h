#pragma once
#include "debug/DebugConfiguration.h"
namespace litecode::debug {
struct TaskDefinition {
    QString label, type;
    QStringList dependencies;
    bool sequential{};
    bool defaultBuild{};
    QJsonObject values;
};
struct TaskCommand {
    QString label, type, command, cwd;
    QStringList arguments;
    QProcessEnvironment environment;
};
class TaskConfigurations final {
  public:
    static QVector<TaskDefinition> load(const QString& workspace, QString* error);
    static QVector<TaskDefinition> parse(const QByteArray& jsonc, QString* error);
    // Returns the dependency closure in topological order, retaining each task's
    // dependency/order metadata for a parallel/sequence scheduler.
    static QVector<TaskDefinition> dependencyGraph(const QVector<TaskDefinition>& tasks,
                                                   const QString& label, QString* error);
    static TaskCommand resolve(const TaskDefinition& task, const VariableContext& context,
                               QString* error);
};
} // namespace litecode::debug
