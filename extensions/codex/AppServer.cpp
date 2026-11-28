#include "codex/AppServer.h"

#include <QDateTime>
#include <QJsonDocument>

namespace litecode::codex {
AppServer::AppServer(QObject* parent) : QObject(parent), process_(new QProcess) {
    connect(process_, &QProcess::readyReadStandardOutput, this,
            [this] { ingest(process_->readAllStandardOutput()); });
    // stderr is diagnostic, not protocol, and may include private prompt/tool data.
    connect(process_, &QProcess::readyReadStandardError, this,
            [this] { process_->readAllStandardError(); });
    connect(process_, &QProcess::bytesWritten, this, [this] { flush(); });
    connect(process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { fail(process_->errorString()); });
    connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                if (reconnecting_) {
                    reconnecting_ = false;
                    fail(tr("Reconnecting Codex…"));
                    start();
                    return;
                }
                fail(tr("Codex stopped (exit %1). Reconnect to continue.").arg(code));
            });
    connect(process_, &QProcess::started, this, [this] {
        request(QStringLiteral("initialize"),
                {{"clientInfo",
                  QJsonObject{{"name", "litecode"},
                              {"title", "LiteCode"},
                              {"version", QStringLiteral(LITECODE_VERSION)}}},
                 {"capabilities", QJsonObject{{"experimentalApi", true}}}},
                [this](const QJsonObject&, const QString& error) {
                    if (!error.isEmpty()) {
                        fail(error);
                        return;
                    }
                    send({{"method", "initialized"}});
                    ready_ = true;
                    emit connected();
                });
    });
    deadlines_.setInterval(1000);
    connect(&deadlines_, &QTimer::timeout, this, [this] {
        const auto now = QDateTime::currentMSecsSinceEpoch();
        const auto keys = pending_.keys();
        for (auto id : keys) {
            if (pending_.value(id).expires > now)
                continue;
            auto callback = pending_.take(id).reply;
            if (callback)
                callback({}, tr("Codex request timed out. Retry or reconnect."));
        }
    });
}
AppServer::~AppServer() {
    // Closing stdin asks app-server to stop gracefully. Detach process ownership so
    // its destructor cannot synchronously wait on the UI thread.
    process_->disconnect(this);
    if (process_->state() == QProcess::NotRunning) {
        delete process_;
        return;
    }
    auto* process = process_;
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
            [process](int, QProcess::ExitStatus) { process->deleteLater(); });
    process->closeWriteChannel();
    QTimer::singleShot(1500, process, [process] {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}
void AppServer::start(const QString& executable) {
    if (process_->state() != QProcess::NotRunning)
        return;
    if (!executable.isEmpty()) executable_ = executable;
    if (executable_.isEmpty()) {
        emit disconnected(tr("Configure the extension's Codex executable before connecting."));
        return;
    }
    input_.clear();
    output_.clear();
    ready_ = false;
    process_->setProgram(executable_);
    process_->setArguments({QStringLiteral("app-server")});
    deadlines_.start();
    process_->start();
}
void AppServer::stop() {
    const auto stoppingPid = process_->processId();
    ready_ = false;
    process_->closeWriteChannel();
    process_->terminate();
    QTimer::singleShot(1500, this, [this, stoppingPid] {
        if (process_->state() != QProcess::NotRunning && process_->processId() == stoppingPid)
            process_->kill();
    });
}
void AppServer::reconnect() {
    if (process_->state() == QProcess::NotRunning) {
        start();
        return;
    }
    reconnecting_ = true;
    stop();
}
qint64 AppServer::request(const QString& method, const QJsonObject& params, Reply reply) {
    if (process_->state() != QProcess::Running || (!ready_ && method != "initialize")) {
        if (reply)
            reply({}, tr("Codex is not connected."));
        return 0;
    }
    const qint64 id = nextId_++;
    pending_.insert(id, {std::move(reply), QDateTime::currentMSecsSinceEpoch() + 120000});
    send({{"id", id}, {"method", method}, {"params", params}});
    return id;
}
void AppServer::respond(const QJsonValue& id, const QJsonObject& result) {
    send({{"id", id}, {"result", result}});
}
void AppServer::reject(const QJsonValue& id, const QString& reason) {
    send({{"id", id}, {"error", QJsonObject{{"code", -32601}, {"message", reason}}}});
}
void AppServer::send(const QJsonObject& message) {
    auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (output_.size() + bytes.size() > 16 * 1024 * 1024) {
        fail(tr("Codex input queue exceeded its safety limit."));
        process_->kill();
        return;
    }
    output_.append(bytes);
    flush();
}
void AppServer::flush() {
    if (output_.isEmpty() || process_->bytesToWrite() > 1024 * 1024)
        return;
    const auto count = process_->write(output_.constData(), qMin<qsizetype>(output_.size(), 65536));
    if (count > 0)
        output_.remove(0, count);
}
void AppServer::ingest(const QByteArray& bytes) {
    input_.append(bytes);
    if (input_.size() > 32 * 1024 * 1024) {
        fail(tr("Codex sent an oversized protocol frame."));
        process_->kill();
        return;
    }
    while (true) {
        const auto newline = input_.indexOf('\n');
        if (newline < 0)
            break;
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(input_.left(newline), &parse);
        input_.remove(0, newline + 1);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
            fail(tr("Invalid Codex protocol frame."));
            process_->kill();
            return;
        }
        const auto message = doc.object();
        if (message.contains("method")) {
            const auto method = message.value("method").toString();
            const auto params = message.value("params").toObject();
            if (message.contains("id"))
                emit interaction(message.value("id"), method, params);
            else
                emit notification(method, params);
        } else if (message.contains("id")) {
            const auto id = message.value("id").toInteger();
            auto callback = pending_.take(id).reply;
            if (callback)
                callback(message.value("result").toObject(),
                         message.value("error").toObject().value("message").toString());
        }
    }
}
void AppServer::fail(const QString& reason) {
    ready_ = false;
    deadlines_.stop();
    input_.clear();
    output_.clear();
    auto pending = std::move(pending_);
    pending_.clear();
    for (auto& value : pending)
        if (value.reply)
            value.reply({}, reason);
    emit disconnected(reason);
}
} // namespace litecode::codex
