#include "debug/TaskConfiguration.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <functional>

namespace litecode::debug {
QVector<TaskDefinition> TaskConfigurations::load(const QString& workspace, QString* error) {
    if (error)
        error->clear();
    if (workspace.isEmpty())
        return {};
    QString path = QDir(workspace).filePath(".vscode/tasks.json");
    if (!QFileInfo::exists(path))
        path = QDir(workspace).filePath(".litecode/tasks.json");
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    return parse(file.read(1024 * 1024 + 1), error);
}
QVector<TaskDefinition> TaskConfigurations::parse(const QByteArray& jsonc, QString* error) {
    QString problem;
    const auto root = DebugConfigurations::parseJsonc(jsonc, &problem);
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QVector<TaskDefinition>{};
    };
    if (error)
        error->clear();
    if (!problem.isEmpty())
        return fail(problem);
    if (root.value("version").toString() != "2.0.0" || !root.value("tasks").isArray())
        return fail("Expected tasks.json version 2.0.0 and tasks array.");
    QVector<TaskDefinition> result;
    QSet<QString> labels;
    for (const auto& value : root.value("tasks").toArray()) {
        if (!value.isObject())
            return fail("Expected task object.");
        auto task = value.toObject();
#ifdef Q_OS_WIN
        const QString platform = "windows";
#elif defined(Q_OS_MACOS)
        const QString platform = "osx";
#else
        const QString platform = "linux";
#endif
        if (task.contains(platform) && !task.value(platform).isObject())
            return fail("Expected task platform override object.");
        const auto override = task.value(platform).toObject();
        for (auto it = override.begin(); it != override.end(); ++it)
            task.insert(it.key(), it.value());
        task.remove("windows");
        task.remove("linux");
        task.remove("osx");
        TaskDefinition definition;
        definition.label = task.value("label").toString();
        definition.type = task.value("type").toString();
        if (definition.label.trimmed().isEmpty() || labels.contains(definition.label))
            return fail("Task labels must be nonempty and unique.");
        labels.insert(definition.label);
        if (task.contains("dependsOn")) {
            const auto depends = task.value("dependsOn");
            if (depends.isString())
                definition.dependencies.append(depends.toString());
            else if (depends.isArray()) {
                for (const auto& dependency : depends.toArray()) {
                    if (!dependency.isString() || dependency.toString().isEmpty())
                        return fail("Expected dependency label.");
                    definition.dependencies.append(dependency.toString());
                }
            } else
                return fail("Expected dependsOn string or array.");
        }
        if (definition.type.isEmpty() && !task.contains("command") &&
            !definition.dependencies.isEmpty())
            definition.type = "compound";
        if (definition.type != "process" && definition.type != "shell" &&
            definition.type != "compound")
            return fail("Unsupported native task type: " + definition.type);
        if (definition.type != "compound" && task.value("command").toString().isEmpty())
            return fail("Task command is required.");
        if (task.contains("args")) {
            if (!task.value("args").isArray())
                return fail("Expected task args array.");
            for (const auto& argument : task.value("args").toArray())
                if (!argument.isString())
                    return fail("Expected string task arguments.");
        }
        const auto order = task.value("dependsOrder").toString("parallel");
        if (order != "parallel" && order != "sequence")
            return fail("Expected parallel or sequence dependency order.");
        definition.sequential = order == "sequence";
        if (task.contains("options") && !task.value("options").isObject())
            return fail("Expected task options object.");
        const auto options = task.value("options").toObject();
        if (options.contains("cwd") && !options.value("cwd").isString())
            return fail("Expected task working directory string.");
        if (options.contains("env") && !options.value("env").isObject())
            return fail("Expected task environment object.");
        const auto environment = options.value("env").toObject();
        for (auto it = environment.begin(); it != environment.end(); ++it)
            if (!it.value().isString() && !it.value().isNull())
                return fail("Environment values must be strings or null.");
        const auto group = task.value("group").toObject();
        definition.defaultBuild =
            group.value("kind").toString() == "build" && group.value("isDefault").toBool();
        definition.values = task;
        result.append(definition);
    }
    return result;
}
QVector<TaskDefinition> TaskConfigurations::dependencyGraph(const QVector<TaskDefinition>& tasks,
                                                            const QString& requested,
                                                            QString* error) {
    if (error)
        error->clear();
    QString label = requested;
    if (label == "${defaultBuildTask}") {
        label.clear();
        for (const auto& task : tasks)
            if (task.defaultBuild) {
                if (!label.isEmpty()) {
                    if (error)
                        *error = "Multiple default build tasks.";
                    return {};
                }
                label = task.label;
            }
        if (label.isEmpty()) {
            if (error)
                *error = "No default build task is configured.";
            return {};
        }
    }
    QHash<QString, TaskDefinition> lookup;
    for (const auto& task : tasks)
        lookup.insert(task.label, task);
    QSet<QString> visiting, visited;
    QVector<TaskDefinition> graph;
    std::function<bool(const QString&, int)> visit = [&](const QString& id, int depth) {
        if (depth > 128 || visiting.contains(id)) {
            if (error)
                *error = "Task dependency cycle or excessive depth: " + id;
            return false;
        }
        if (visited.contains(id))
            return true;
        const auto it = lookup.constFind(id);
        if (it == lookup.cend()) {
            if (error)
                *error = "Task not found: " + id;
            return false;
        }
        visiting.insert(id);
        for (const auto& dependency : it->dependencies)
            if (!visit(dependency, depth + 1))
                return false;
        visiting.remove(id);
        visited.insert(id);
        graph.append(*it);
        return true;
    };
    return visit(label, 0) ? graph : QVector<TaskDefinition>{};
}
TaskCommand TaskConfigurations::resolve(const TaskDefinition& task, const VariableContext& context,
                                        QString* error) {
    QString problem;
    const QJsonObject execution{{"command", task.values.value("command")},
                                {"args", task.values.value("args")},
                                {"options", task.values.value("options")}};
    const auto resolved = DebugConfigurations::resolve(execution, context, &problem).toObject();
    if (error)
        *error = problem;
    if (!problem.isEmpty())
        return {};
    TaskCommand command;
    command.label = task.label;
    command.type = task.type;
    command.command = resolved.value("command").toString();
    for (const auto& argument : resolved.value("args").toArray())
        command.arguments.append(argument.toString());
    const auto options = resolved.value("options").toObject();
    command.cwd = options.value("cwd").toString(context.workspaceFolder);
    if (QDir::isRelativePath(command.cwd))
        command.cwd = QDir(context.workspaceFolder).absoluteFilePath(command.cwd);
    command.environment = context.environment;
    const auto environment = options.value("env").toObject();
    for (auto it = environment.begin(); it != environment.end(); ++it) {
        if (it.value().isNull())
            command.environment.remove(it.key());
        else
            command.environment.insert(it.key(), it.value().toString());
    }
    return command;
}
} // namespace litecode::debug
