#include "PythonInterpreters.h"
#include "PythonProcess.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <stdexcept>
namespace litecode::python {
QJsonArray Interpreter::command() const {
    QJsonArray result{executable};
    for (const auto& arg : arguments)
        result.append(arg);
    return result;
}
PythonInterpreters::PythonInterpreters(extensions::ExtensionContext& context) : context_(context) {}
QStringList PythonInterpreters::executableCommand(const QJsonValue& value) {
    QStringList command;
    if (value.isString())
        command.append(value.toString());
    else if (value.isArray()) {
        for (const auto& item : value.toArray()) {
            if (!item.isString())
                throw std::runtime_error("Python command entries must be strings.");
            command.append(item.toString());
        }
    } else if (!value.isUndefined() && !value.isNull())
        throw std::runtime_error(
            "Python must be an executable path or an array of command arguments.");
    if (command.isEmpty() || command.first().trimmed().isEmpty())
        return {};
    for (const auto& arg : command)
        if (arg.contains(QChar::Null))
            throw std::runtime_error("Python command contains a null character.");
    return command;
}
QStringList PythonInterpreters::candidates() const {
    QStringList values;
    const auto add = [&](const QString& path) {
        if (!path.isEmpty() && !values.contains(path))
            values.append(path);
    };
    add(context_.workspaceState().get("selectedInterpreter", "").toString());
    const auto configured = context_.workspace()
                                .getConfiguration("python")
                                ->get("defaultInterpreterPath", "python")
                                .toString();
    if (configured != "python")
        add(configured);
    const QDir root(context_.workspacePath());
    if (!context_.workspacePath().isEmpty())
        for (const auto& directory : {".venv", "venv", ".env"}) {
#ifdef Q_OS_WIN
            add(root.filePath(QString(directory) + "/Scripts/python.exe"));
#else
            add(root.filePath(QString(directory) + "/bin/python"));
#endif
        }
    const auto virtualEnv = qEnvironmentVariable("VIRTUAL_ENV");
    if (!virtualEnv.isEmpty())
        add(virtualEnv);
#ifdef Q_OS_WIN
    // PEP 514 registration finds ordinary Python installs even without PATH.
    for (const auto* hive : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
        QSettings registry(QString(hive) + "\\Software\\Python\\PythonCore",
                           QSettings::NativeFormat);
        auto versions = registry.childGroups();
        std::sort(versions.begin(), versions.end(), std::greater<QString>());
        for (const auto& version : versions)
            add(registry.value(version + "/InstallPath/ExecutablePath").toString());
    }
#endif
    add(QStandardPaths::findExecutable("python3"));
    add(QStandardPaths::findExecutable("python"));
#ifdef Q_OS_WIN
    add(QStandardPaths::findExecutable("py"));
#endif
    values.removeAll(QString{});
    return values;
}
QFuture<std::optional<Interpreter>> PythonInterpreters::probe(QStringList command,
                                                              extensions::CancellationToken token) {
    auto executable = command.takeFirst();
    executable.replace("${workspaceFolder}", context_.workspacePath());
    if (QFileInfo(executable).isDir()) {
#ifdef Q_OS_WIN
        executable = QDir(executable).filePath("Scripts/python.exe");
#else
        executable = QDir(executable).filePath("bin/python");
#endif
    }
    auto args = command;
    args.append(
        {"-I", "-c",
         "import json,sys; "
         "print(json.dumps({'executable':sys.executable,'version':list(sys.version_info[:3])}))"});
    return runPythonProcess(*this, executable, args, context_.workspacePath(), token)
        .then(this,
              [command, executable,
               token](const ProcessResult& result) -> std::optional<Interpreter> {
                  if (token.isCancellationRequested() || result.exitCode)
                      return {};
                  const auto object = QJsonDocument::fromJson(result.output.trimmed()).object();
                  const auto version = object.value("version").toArray();
                  const auto path = object.value("executable").toString();
                  if (version.size() != 3 || version[0].toInt() != 3 || version[1].toInt() < 10 ||
                      path.isEmpty() || !QFileInfo(path).isFile())
                      return {};
                  return Interpreter{path,
                                     QString("%1.%2.%3")
                                         .arg(version[0].toInt())
                                         .arg(version[1].toInt())
                                         .arg(version[2].toInt()),
                                     QFileInfo(executable).baseName() == "py" ? QStringList{}
                                                                              : command};
              });
}
QFuture<Interpreter> PythonInterpreters::resolve(const QJsonValue& configured,
                                                 extensions::CancellationToken token) {
    const auto explicitCommand = executableCommand(configured);
    auto promise = std::make_shared<QPromise<Interpreter>>();
    promise->start();
    auto result = promise->future();
    auto commands = std::make_shared<QVector<QStringList>>();
    if (!explicitCommand.isEmpty())
        commands->append(explicitCommand);
    else
        for (const auto& candidate : candidates())
            commands->append(QStringList{candidate});
    auto index = std::make_shared<int>(0);
    auto step = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weak = step;
    *step = [this, promise, commands, index, token, weak] {
        if (token.isCancellationRequested() || *index >= commands->size()) {
            promise->setException(std::make_exception_ptr(
                std::runtime_error(token.isCancellationRequested()
                                       ? "Python discovery cancelled."
                                       : "Python 3.10 or newer was not found. Install Python or "
                                         "use Python Debugger: Select Interpreter.")));
            promise->finish();
            return;
        }
        const auto keep = weak.lock();
        probe(commands->at((*index)++), token)
            .then(this, [promise, keep](const std::optional<Interpreter>& value) {
                if (value) {
                    promise->addResult(*value);
                    promise->finish();
                } else
                    (*keep)();
            });
    };
    const auto lifetime = connect(this, &QObject::destroyed, [promise] {
        if (!promise->future().isFinished()) {
            promise->setException(
                std::make_exception_ptr(std::runtime_error("Python extension stopped.")));
            promise->finish();
        }
    });
    result.then([lifetime](const Interpreter&) { QObject::disconnect(lifetime); })
        .onFailed([lifetime] { QObject::disconnect(lifetime); });
    (*step)();
    return result;
}
QFuture<std::optional<Interpreter>>
PythonInterpreters::select(extensions::CancellationToken token) {
    // The common input component accepts executable paths with spaces literally.
    extensions::InputBoxOptions options;
    options.title = "Select Python Interpreter";
    options.prompt = "Python executable or virtual-environment directory (Python 3.10+)";
    options.value = context_.workspaceState().get("selectedInterpreter", "").toString();
    return context_.window()
        .showInputBox(options, token)
        .then(this,
              [this,
               token](const std::optional<QString>& value) -> QFuture<std::optional<Interpreter>> {
                  if (!value || value->trimmed().isEmpty()) {
                      QPromise<std::optional<Interpreter>> empty;
                      empty.start();
                      empty.addResult(std::nullopt);
                      empty.finish();
                      return empty.future();
                  }
                  return resolve(*value, token)
                      .then(this,
                            [this](const Interpreter& interpreter) -> std::optional<Interpreter> {
                                context_.workspaceState().update("selectedInterpreter",
                                                                 interpreter.executable);
                                return interpreter;
                            });
              })
        .unwrap();
}
} // namespace litecode::python
