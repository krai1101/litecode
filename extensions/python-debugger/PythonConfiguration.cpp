#include "PythonConfiguration.h"
#include <QDir>
#include <QFileInfo>
#include <stdexcept>
namespace litecode::python {
QVector<QJsonObject> pythonConfigurations(const QString& workspace, bool initial) {
    QVector<QJsonObject> result{{{"name", "Python Debugger: Python File"},
                                 {"type", "debugpy"},
                                 {"request", "launch"},
                                 {"program", "${file}"},
                                 {"console", "integratedTerminal"}}};
    if (initial)
        return result;
    result.append({{"name", "Python Debugger: Module"},
                   {"type", "debugpy"},
                   {"request", "launch"},
                   {"module", ""},
                   {"console", "integratedTerminal"}});
    result.append({{"name", "Python Debugger: Remote Attach"},
                   {"type", "debugpy"},
                   {"request", "attach"},
                   {"connect", QJsonObject{{"host", "127.0.0.1"}, {"port", 5678}}}});
    if (!workspace.isEmpty() && QFileInfo(QDir(workspace).filePath("manage.py")).isFile())
        result.append({{"name", "Python Debugger: Django"},
                       {"type", "debugpy"},
                       {"request", "launch"},
                       {"program", "${workspaceFolder}/manage.py"},
                       {"args", QJsonArray{"runserver", "--noreload"}},
                       {"django", true},
                       {"console", "integratedTerminal"}});
    return result;
}
QJsonObject preparePythonConfiguration(QJsonObject config, const QString& workspace,
                                       const QString& activeFile, bool justMyCode,
                                       const QString& envFile) {
    if (config.isEmpty())
        config = pythonConfigurations(workspace, true).first();
    if (!config.contains("type"))
        config.insert("type", "debugpy");
    if (!config.contains("request"))
        config.insert("request", "launch");
    if (!config.contains("name"))
        config.insert("name", "Python Debugger");
    if (!config.contains("justMyCode"))
        config.insert("justMyCode", justMyCode);
    if (config.value("request") == "launch") {
        if (!config.contains("program") && !config.contains("module") && !config.contains("code"))
            config.insert("program", activeFile);
        if (config.value("program") == "${file}" &&
            (activeFile.isEmpty() || (!activeFile.endsWith(".py", Qt::CaseInsensitive) &&
                                      !activeFile.endsWith(".pyw", Qt::CaseInsensitive))))
            throw std::runtime_error("Open a saved Python file before starting Python debugging.");
        if (!config.contains("cwd"))
            config.insert("cwd",
                          workspace.isEmpty() ? QFileInfo(activeFile).absolutePath() : workspace);
        if (!config.contains("console"))
            config.insert("console", "integratedTerminal");
        if (!config.contains("redirectOutput"))
            config.insert("redirectOutput", config.value("console") == "internalConsole");
        if (!config.contains("envFile") && !envFile.isEmpty() && !workspace.isEmpty())
            config.insert("envFile", envFile);
        if (config.contains("pythonPath") && !config.contains("python"))
            config.insert("python", config.take("pythonPath"));
    }
    return config;
}
bool pythonRemoteConnect(const QJsonObject& config) {
    return config.value("request") == "attach" &&
           (config.contains("connect") || config.contains("port"));
}
void validatePythonConfiguration(const QJsonObject& config) {
    if (config.value("type") != "debugpy")
        throw std::runtime_error("Python Debugger requires type 'debugpy'.");
    const auto request = config.value("request").toString();
    if (request == "launch") {
        int targets = 0;
        for (const auto* key : {"program", "module", "code"}) {
            if (!config.contains(key))
                continue;
            if (!config.value(key).isString() || config.value(key).toString().isEmpty())
                throw std::runtime_error(
                    "Python program, module or code must be a non-empty string.");
            ++targets;
        }
        if (targets != 1)
            throw std::runtime_error(
                "Python launch requires exactly one of program, module or code.");
        if (config.contains("program") && !QFileInfo(config.value("program").toString()).isFile())
            throw std::runtime_error("The configured Python program does not exist.");
        const auto console = config.value("console").toString();
        if (console != "internalConsole" && console != "integratedTerminal")
            throw std::runtime_error("Invalid Python debug console setting.");
        if (!QFileInfo(config.value("cwd").toString()).isDir())
            throw std::runtime_error("The Python working directory does not exist.");
    } else if (request == "attach") {
        const int endpoints = int(config.contains("connect") || config.contains("port")) +
                              int(config.contains("listen")) + int(config.contains("processId"));
        if (endpoints != 1)
            throw std::runtime_error(
                "Python attach requires exactly one of connect, listen or processId.");
        for (const auto* key : {"connect", "listen"}) {
            if (!config.contains(key))
                continue;
            if (!config.value(key).isObject())
                throw std::runtime_error("Python attach endpoint must be an object.");
            const auto endpoint = config.value(key).toObject();
            const auto portValue = endpoint.value("port");
            bool ok = false;
            const int port =
                portValue.isString() ? portValue.toString().toInt(&ok) : portValue.toInt(-1);
            if (!portValue.isString())
                ok = portValue.isDouble() && portValue.toDouble() == port;
            if (!ok || port < 1 || port > 65535)
                throw std::runtime_error("Python attach port must be between 1 and 65535.");
            if (endpoint.contains("host") &&
                (!endpoint.value("host").isString() || endpoint.value("host").toString().isEmpty()))
                throw std::runtime_error("Python attach host must be a non-empty string.");
        }
        if (config.contains("processId")) {
            const auto value = config.value("processId");
            bool valid = false;
            const auto pid =
                value.isString() ? value.toString().toLongLong(&valid) : value.toInteger(-1);
            if (!value.isString())
                valid = value.isDouble() && value.toDouble() == double(pid);
            if (!valid || pid <= 0)
                throw std::runtime_error("Python processId must be a positive process ID.");
        }
    } else
        throw std::runtime_error("Python request must be launch or attach.");
    if (config.contains("args")) {
        if (!config.value("args").isArray())
            throw std::runtime_error("Python args must be an array of strings.");
        for (const auto& value : config.value("args").toArray())
            if (!value.isString())
                throw std::runtime_error("Python args must be an array of strings.");
    }
}
} // namespace litecode::python
