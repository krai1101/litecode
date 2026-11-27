#include "extensions/HostOutput.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QUuid>
#include <stdexcept>

namespace litecode::extensions {
struct OutputState {
    QString id, name, file, language;
    bool log{}, disposed{};
    LogLevel level{LogLevel::Info};
    std::optional<LogLevel> levelOverride;
    EventEmitter<LogLevel> changed;
};
struct OutputRuntime {
    QMutex mutex;
    QObject* dispatcher{};
    QString logs;
    bool stopped{};
    LogLevel globalLevel{LogLevel::Info};
    std::function<void(const QJsonObject&)> send;
    QHash<QString, std::shared_ptr<OutputState>> channels;
    // Like upstream's log-file registry, retain the public channel until explicit
    // disposal. Dropping an extension's handle must not create a second logger
    // for the same file on a later createOutputChannel call.
    QHash<QString, std::shared_ptr<LogOutputChannel>> logChannels;
};
namespace {
// The caller holds runtime.mutex while enqueueing. Destruction clears the
// dispatcher under that same lock, preventing cross-thread use-after-free.
void post(const std::shared_ptr<OutputRuntime>& runtime, const std::shared_ptr<OutputState>& channel,
    QString operation, QString text = {}, bool preserveFocus = false) {
    QMetaObject::invokeMethod(runtime->dispatcher, [runtime, channel, operation, text, preserveFocus] {
        { QMutexLocker lock(&runtime->mutex); if (runtime->stopped) return; }
        if (operation == "append" && !text.isEmpty()) {
            QFile file(channel->file);
            const auto bytes = text.toUtf8();
            if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(bytes) != bytes.size()) {
                runtime->send({{"event", "output"}, {"id", channel->id}, {"operation", "error"},
                    {"text", "Could not write output log: " + file.errorString()}});
                return;
            }
        }
        if (operation == "append") {
            for (qsizetype offset = 0; offset < text.size();) {
                auto length = qMin<qsizetype>(32768, text.size() - offset);
                if (offset + length < text.size() && text.at(offset + length - 1).isHighSurrogate()) --length;
                runtime->send({{"event", "output"}, {"id", channel->id}, {"operation", "append"}, {"text", text.mid(offset, length)}});
                offset += length;
            }
        } else {
            QJsonObject message{{"event", "output"}, {"id", channel->id}, {"operation", operation}};
            if (operation == "create") {
                message.insert("name", channel->name);
                message.insert("languageId", channel->language);
                message.insert("log", channel->log);
                message.insert("level", static_cast<int>(channel->level));
            }
            if (operation == "show") message.insert("preserveFocus", preserveFocus);
            runtime->send(message);
        }
    }, Qt::QueuedConnection);
}
QString argument(const QJsonValue& value) {
    if (value.isString()) return value.toString();
    if (value.isNull()) return QStringLiteral("null");
    if (value.isUndefined()) return QStringLiteral("undefined");
    const auto encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}
class Channel final : public LogOutputChannel {
  public:
    Channel(std::shared_ptr<OutputRuntime> runtime, std::shared_ptr<OutputState> state)
        : runtime_(std::move(runtime)), state_(std::move(state)) {}
    QString name() const override { return state_->name; }
    bool closed() const { return state_->disposed; } // Runtime mutex held by creator.
    LogLevel logLevel() const override { QMutexLocker lock(&runtime_->mutex); return state_->level; }
    Event<LogLevel> onDidChangeLogLevel() const override { return state_->changed.event(); }
    void append(const QString& value) override {
        if (state_->log) { info(value, {}); return; }
        QMutexLocker lock(&runtime_->mutex); validate();
        if (!value.isEmpty()) post(runtime_, state_, "append", value);
    }
    void appendLine(const QString& value) override {
        if (state_->log) info(value, {});
        else append(value + QLatin1Char('\n'));
    }
    void clear() override { QMutexLocker lock(&runtime_->mutex); validate(); post(runtime_, state_, "clear"); }
    void replace(const QString& value) override {
        QMutexLocker lock(&runtime_->mutex); validate();
        post(runtime_, state_, "clear");
        if (state_->log) write(LogLevel::Info, value, {});
        else if (!value.isEmpty()) post(runtime_, state_, "append", value);
    }
    void show(bool preserveFocus) override { QMutexLocker lock(&runtime_->mutex); validate(); post(runtime_, state_, "show", {}, preserveFocus); }
    void hide() override { QMutexLocker lock(&runtime_->mutex); validate(); post(runtime_, state_, "hide"); }
    void dispose() override {
        QMutexLocker lock(&runtime_->mutex);
        if (state_->disposed) return;
        state_->disposed = true;
        state_->changed.dispose();
        if (!runtime_->stopped) post(runtime_, state_, "dispose");
        runtime_->channels.remove(state_->id);
        for (auto it = runtime_->logChannels.begin(); it != runtime_->logChannels.end();) {
            if (it.value().get() == this) it = runtime_->logChannels.erase(it);
            else ++it;
        }
    }
    void trace(const QString& message, const QJsonArray& args) override { log(LogLevel::Trace, message, args); }
    void debug(const QString& message, const QJsonArray& args) override { log(LogLevel::Debug, message, args); }
    void info(const QString& message, const QJsonArray& args) override { log(LogLevel::Info, message, args); }
    void warn(const QString& message, const QJsonArray& args) override { log(LogLevel::Warning, message, args); }
    void error(const QString& message, const QJsonArray& args) override { log(LogLevel::Error, message, args); }
  private:
    void validate() const { if (state_->disposed || runtime_->stopped) throw std::runtime_error("Channel has been closed."); }
    void log(LogLevel level, const QString& message, const QJsonArray& args) {
        QMutexLocker lock(&runtime_->mutex); validate(); write(level, message, args);
    }
    void write(LogLevel level, QString message, const QJsonArray& args) {
        if (state_->level == LogLevel::Off || level < state_->level) return;
        for (const auto& value : args) message += QLatin1Char(' ') + argument(value);
        static const char* labels[] = {"off", "trace", "debug", "info", "warning", "error"};
        post(runtime_, state_, "append", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") +
            " [" + labels[static_cast<int>(level)] + "] " + message + '\n');
    }
    std::shared_ptr<OutputRuntime> runtime_;
    std::shared_ptr<OutputState> state_;
};
} // namespace
HostOutput::HostOutput(QString logs, std::function<void(const QJsonObject&)> send) : runtime_(std::make_shared<OutputRuntime>()) {
    if (!QDir().mkpath(logs)) throw std::runtime_error("Could not create output log directory.");
    runtime_->dispatcher = this; runtime_->logs = std::move(logs); runtime_->send = std::move(send);
}
HostOutput::~HostOutput() { shutdown(); }
std::shared_ptr<OutputChannel> HostOutput::createOutputChannel(const QString& name, const std::optional<QString>& language) {
    return create(name, language, false);
}
std::shared_ptr<LogOutputChannel> HostOutput::createOutputChannel(const QString& name, LogOutputChannelOptions) {
    return create(name, {}, true);
}
std::shared_ptr<LogOutputChannel> HostOutput::create(const QString& name, const std::optional<QString>& language, bool log) {
    const auto label = name.trimmed();
    if (label.isEmpty() || (language && language->trimmed().isEmpty())) throw std::invalid_argument("Output name/language must not be empty.");
    auto fileKey = label;
    fileKey.remove(QRegularExpression(QStringLiteral(R"([/\\:*?"<>|])")));
#ifdef Q_OS_WIN
    fileKey = fileKey.toCaseFolded();
#endif
    QMutexLocker lock(&runtime_->mutex);
    if (runtime_->stopped) throw std::runtime_error("Output service stopped.");
    if (log) {
        const auto previous = runtime_->logChannels.value(fileKey);
        if (previous && !std::static_pointer_cast<Channel>(previous)->closed()) return previous;
    }
    auto state = std::make_shared<OutputState>();
    state->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state->name = label; state->language = language.value_or("log"); state->log = log;
    if (log) state->level = runtime_->globalLevel;
    const auto file = log ? QString::fromLatin1(QCryptographicHash::hash(fileKey.toUtf8(), QCryptographicHash::Sha256).toHex()) : state->id;
    state->file = QDir(runtime_->logs).filePath(file + ".log");
    auto channel = std::make_shared<Channel>(runtime_, state);
    runtime_->channels.insert(state->id, state);
    if (log) runtime_->logChannels.insert(fileKey, channel);
    post(runtime_, state, "create");
    return channel;
}
void HostOutput::setLogLevel(const QString& id, LogLevel level) {
    if (static_cast<int>(level) < 0 || static_cast<int>(level) > 5) return;
    std::shared_ptr<OutputState> state;
    bool changed = false;
    {
        QMutexLocker lock(&runtime_->mutex);
        state = runtime_->channels.value(id);
        if (runtime_->stopped || !state || !state->log || state->disposed) return;
        state->levelOverride = level == runtime_->globalLevel ? std::nullopt : std::optional(level);
        changed = state->level != level;
        state->level = level;
    }
    if (changed) state->changed.fire(level);
}
void HostOutput::setGlobalLogLevel(LogLevel level) {
    if (static_cast<int>(level) < 0 || static_cast<int>(level) > 5) return;
    const auto runtime = runtime_;
    QVector<std::shared_ptr<OutputState>> changed;
    {
        QMutexLocker lock(&runtime->mutex);
        if (runtime->stopped || runtime->globalLevel == level) return;
        runtime->globalLevel = level;
        for (const auto& state : runtime->channels) {
            if (!state->log || state->disposed || state->levelOverride || state->level == level) continue;
            state->level = level;
            changed.append(state);
        }
    }
    for (const auto& state : changed) {
        {
            QMutexLocker lock(&runtime->mutex);
            if (runtime->stopped || runtime->globalLevel != level) return;
            if (state->disposed || state->level != level) continue;
        }
        state->changed.fire(level);
    }
}
LogLevel HostOutput::globalLogLevel() const {
    QMutexLocker lock(&runtime_->mutex);
    return runtime_->globalLevel;
}
void HostOutput::clear(const QString& id) {
    QMutexLocker lock(&runtime_->mutex);
    const auto state = runtime_->channels.value(id);
    if (runtime_->stopped || !state || state->disposed) return;
    post(runtime_, state, "clear");
}
void HostOutput::shutdown() {
    QMutexLocker lock(&runtime_->mutex);
    if (runtime_->stopped) return;
    runtime_->stopped = true; runtime_->dispatcher = nullptr;
    for (const auto& state : runtime_->channels) { state->disposed = true; state->changed.dispose(); }
    runtime_->channels.clear(); runtime_->logChannels.clear();
}
} // namespace litecode::extensions
