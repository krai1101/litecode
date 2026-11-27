#include "JavaLanguageServer.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>

namespace litecode::java {
namespace {
QString javaExecutable(extensions::ExtensionContext& context) {
    const QString configured =
        context.workspace().getConfiguration("java")->get("jdt.ls.java.home").toString();
    const auto javaHome = configured.isEmpty() ? qEnvironmentVariable("JAVA_HOME") : configured;
    if (!javaHome.isEmpty()) {
#ifdef Q_OS_WIN
        const auto path = QDir(javaHome).filePath("bin/java.exe");
#else
        const auto path = QDir(javaHome).filePath("bin/java");
#endif
        if (QFileInfo(path).isExecutable())
            return path;
        if (!configured.isEmpty())
            throw std::runtime_error("java.jdt.ls.java.home must point to a JDK 21 or newer.");
    }
    const auto path = QStandardPaths::findExecutable("java");
    if (path.isEmpty())
        throw std::runtime_error("Install JDK 21 or newer and set JAVA_HOME or add java to PATH.");
    return path;
}
} // namespace

JavaLanguageServer::JavaLanguageServer(extensions::ExtensionContext& context)
    : context_(context), log_(context.window().createOutputChannel(
                             "Java Debugger", extensions::LogOutputChannelOptions{})) {
    startupTimeout_.setSingleShot(true);
    connect(&startupTimeout_, &QTimer::timeout, this, [this] {
        reset("The Java language server did not finish starting.");
        process_.kill();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        for (const auto& message : decoder_.feed(process_.readAllStandardOutput()))
            receive(message);
        if (!decoder_.error().isEmpty()) {
            reset(decoder_.error());
            process_.kill();
        }
    });
    connect(&process_, &QProcess::readyReadStandardError, this,
            [this] { log_->append(QString::fromUtf8(process_.readAllStandardError())); });
    connect(&process_, &QProcess::errorOccurred, this, [this] { reset(process_.errorString()); });
    connect(&process_, &QProcess::finished, this, [this] {
        reset("The Java language server stopped. Check the Java Debugger output and your JDK "
              "version.");
    });
    connect(&context_, &extensions::ExtensionContext::workspaceChanged, this,
            [this](const QString&) {
                if (process_.state() != QProcess::NotRunning)
                    process_.kill();
            });
}

JavaLanguageServer::~JavaLanguageServer() {
    disconnect(&process_, nullptr, this, nullptr);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1000);
    }
    reset("The Java debugger extension was stopped.");
    log_->dispose();
}

QFuture<QJsonValue> JavaLanguageServer::ensureStarted(const QString& workspace) {
    if (process_.state() != QProcess::NotRunning) {
        if (workspace == workspace_ && ready_.isValid())
            return ready_;
        throw std::runtime_error("The Java language server is switching workspaces. Try again.");
    }
    const auto java = javaExecutable(context_);
    const auto runtime = context_.asAbsolutePath("bundled/jdtls");
    const auto plugin =
        context_.asAbsolutePath("bundled/com.microsoft.java.debug.plugin-0.53.2.jar");
    QDirIterator launchers(QDir(runtime).filePath("plugins"),
                           {"org.eclipse.equinox.launcher_*.jar"}, QDir::Files);
    if (!launchers.hasNext() || !QFileInfo(plugin).isFile())
        throw std::runtime_error(
            "Reinstall the Java Debugger extension. Its server files are missing.");
    const auto launcher = launchers.next();
#ifdef Q_OS_WIN
    const QString platform = "win";
#elif defined(Q_OS_MACOS)
    const QString platform = "mac";
#else
    const QString platform = "linux";
#endif
    auto configPath = QDir(runtime).filePath("config_" + platform);
#if defined(Q_PROCESSOR_ARM_64)
    if (QFileInfo(configPath + "_arm").isDir())
        configPath += "_arm";
#endif
    data_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath("litecode-jdtls-XXXXXX"));
    if (!data_->isValid())
        throw std::runtime_error("Could not create a Java language server data directory.");
    workspace_ = workspace;
    decoder_.reset();
    const auto startup = std::make_shared<QPromise<QJsonValue>>();
    startup->start();
    startup_ = startup;
    ready_ = startup->future();
    startupTimeout_.start(120000);
    process_.setWorkingDirectory(workspace);
    process_.start(
        java,
        {"-Declipse.application=org.eclipse.jdt.ls.core.id1", "-Dosgi.bundles.defaultStartLevel=4",
         "-Declipse.product=org.eclipse.jdt.ls.core.product", "-Dosgi.checkConfiguration=true",
         "-Dosgi.sharedConfiguration.area=" +
             QUrl::fromLocalFile(configPath).toString(QUrl::FullyEncoded),
         "--add-modules=ALL-SYSTEM", "--add-opens=java.base/java.util=ALL-UNNAMED",
         "--add-opens=java.base/java.lang=ALL-UNNAMED", "-jar", launcher, "-configuration",
         data_->filePath("configuration"), "-data", data_->filePath("workspace")});
    const auto uri = QUrl::fromLocalFile(workspace).toString(QUrl::FullyEncoded);
    const QJsonObject settings{
        {"java",
         QJsonObject{{"configuration", QJsonObject{{"updateBuildConfiguration", "automatic"}}}}}};
    // JDT LS acknowledges initialize before its workspace import is ready.
    // Wait for ServiceReady before running project or debugger commands.
    (void)request(
        "initialize",
        {{"processId", QCoreApplication::applicationPid()},
         {"rootUri", uri},
         {"capabilities", QJsonObject{{"workspace", QJsonObject{{"configuration", true},
                                                                {"workspaceFolders", true}}}}},
         {"workspaceFolders",
          QJsonArray{QJsonObject{{"uri", uri}, {"name", QFileInfo(workspace).fileName()}}}},
         {"initializationOptions", QJsonObject{{"bundles", QJsonArray{plugin}},
                                               {"workspaceFolders", QJsonArray{uri}},
                                               {"settings", settings}}}})
        .then(this,
              [this, startup](QJsonValue) {
                  if (startup_ == startup)
                      send({{"method", "initialized"}, {"params", QJsonObject{}}});
              })
        .onFailed(this, [this, startup](const std::exception& error) {
            if (startup_ != startup)
                return;
            reset(QString::fromUtf8(error.what()));
            process_.kill();
        });
    return ready_;
}

QFuture<QJsonValue> JavaLanguageServer::execute(const QString& workspace, const QString& command,
                                                QJsonArray arguments) {
    return ensureStarted(workspace)
        .then(this,
              [this, command, arguments](QJsonValue) {
                  return request("workspace/executeCommand",
                                 {{"command", command}, {"arguments", arguments}});
              })
        .unwrap();
}

QFuture<QJsonValue> JavaLanguageServer::request(const QString& method, QJsonObject params) {
    const int id = ++sequence_;
    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();
    const auto future = promise->future();
    pending_.insert(id, promise);
    send({{"id", id}, {"method", method}, {"params", params}});
    QTimer::singleShot(120000, this, [this, id, method] {
        if (auto pending = pending_.take(id)) {
            send({{"method", "$/cancelRequest"}, {"params", QJsonObject{{"id", id}}}});
            pending->setException(std::make_exception_ptr(std::runtime_error(
                ("The Java language server did not respond to " + method + ".").toStdString())));
            pending->finish();
            if (method == "initialize")
                process_.kill();
        }
    });
    return future;
}

void JavaLanguageServer::send(const QJsonObject& message) {
    auto frame = message;
    frame.insert("jsonrpc", "2.0");
    process_.write(debug::DapDecoder::encode(frame));
}

void JavaLanguageServer::receive(const QJsonObject& message) {
    const auto method = message.value("method").toString();
    const auto id = message.value("id");
    if (!method.isEmpty() && !id.isUndefined()) {
        const auto parameters = message.value("params").toObject();
        QJsonValue result(QJsonValue::Null);
        if (method == "workspace/configuration") {
            QJsonArray items;
            for (const auto& item : parameters.value("items").toArray()) {
                const auto section = item.toObject().value("section").toString();
                const auto value = context_.workspace().getConfiguration()->get(section);
                items.append(value.isUndefined() ? QJsonValue(QJsonValue::Null) : value);
            }
            result = items;
        } else if (method == "workspace/workspaceFolders") {
            result = QJsonArray{
                QJsonObject{{"uri", QUrl::fromLocalFile(workspace_).toString(QUrl::FullyEncoded)},
                            {"name", QFileInfo(workspace_).fileName()}}};
        } else if (method == "workspace/applyEdit") {
            result = QJsonObject{{"applied", false},
                                 {"failureReason", "Java Debugger does not apply editor changes."}};
        } else if (method != "client/registerCapability" &&
                   method != "client/unregisterCapability" &&
                   method != "window/workDoneProgress/create" &&
                   method != "window/showMessageRequest") {
            send({{"id", id},
                  {"error", QJsonObject{{"code", -32601}, {"message", "Unsupported request"}}}});
            return;
        }
        send({{"id", id}, {"result", result}});
    } else if (!method.isEmpty()) {
        const auto parameters = message.value("params").toObject();
        if (method == "language/status" && parameters.value("type") == "ServiceReady") {
            if (auto startup = std::exchange(startup_, {})) {
                startupTimeout_.stop();
                startup->addResult(QJsonValue{});
                startup->finish();
            }
        } else if (method == "window/logMessage" || method == "window/showMessage") {
            log_->appendLine(parameters.value("message").toString());
        }
    } else if (auto pending = pending_.take(id.toInt())) {
        if (message.contains("error"))
            pending->setException(std::make_exception_ptr(
                std::runtime_error(message.value("error")
                                       .toObject()
                                       .value("message")
                                       .toString("Java language server request failed.")
                                       .toStdString())));
        else
            pending->addResult(message.value("result"));
        pending->finish();
    }
}

void JavaLanguageServer::reset(const QString& error) {
    startupTimeout_.stop();
    ready_ = {};
    if (auto startup = std::exchange(startup_, {})) {
        startup->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
        startup->finish();
    }
    for (const auto& pending : std::exchange(pending_, {})) {
        pending->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
        pending->finish();
    }
}
} // namespace litecode::java
