#include "extensions/HostConfiguration.h"

#include <QMutexLocker>
#include <QJsonObject>
#include <QPointer>
#include <QThread>

namespace litecode::extensions {
namespace {

ConfigurationResource resource(const std::optional<ConfigurationScope>& scope) {
    return scope ? ConfigurationResource{scope->uri, scope->languageId} : ConfigurationResource{};
}

void fail(const std::shared_ptr<QPromise<void>>& promise, const QString& error) {
    promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    promise->finish();
}

QFuture<void> failedFuture(const QString& error) {
    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    const auto future = promise->future();
    fail(promise, error);
    return future;
}

class SnapshotConfiguration final : public WorkspaceConfiguration {
  public:
    SnapshotConfiguration(ConfigurationModel model, QString section,
                          std::optional<ConfigurationScope> scope, QPointer<HostConfiguration> owner)
        : model_(std::move(model)), section_(std::move(section)), scope_(std::move(scope)), owner_(owner) {}
    QJsonValue get(const QString& section) const override {
        return model_.get(section.isEmpty() ? section_ : key(section), resource(scope_));
    }
    bool has(const QString& section) const override { return !get(section).isUndefined(); }
    std::optional<ConfigurationInspect> inspect(const QString& section) const override {
        const auto value = model_.inspect(key(section), resource(scope_));
        if (!value) return std::nullopt;
        return ConfigurationInspect{value->key, value->defaultValue, value->globalValue, value->workspaceValue,
            value->workspaceFolderValue, value->defaultLanguageValue, value->globalLanguageValue,
            value->workspaceLanguageValue, value->workspaceFolderLanguageValue,
            value->languageIds.isEmpty() ? std::nullopt : std::optional<QStringList>(value->languageIds)};
    }
    QFuture<void> update(const QString& section, const QJsonValue& value,
                        ConfigurationUpdateTarget target, std::optional<bool> overrideInLanguage) override {
        if (!owner_) return failedFuture("Extension configuration host is no longer available.");
        return owner_->requestUpdate(key(section), value, scope_, target, overrideInLanguage);
    }
    QJsonValue operator[](const QString& key) const override {
        return model_.get(section_, resource(scope_)).toObject().value(key);
    }

  private:
    QString key(const QString& value) const { return section_.isEmpty() ? value : section_ + '.' + value; }
    ConfigurationModel model_;
    QString section_;
    std::optional<ConfigurationScope> scope_;
    QPointer<HostConfiguration> owner_;
};

} // namespace

HostConfiguration::HostConfiguration(std::function<void(const QJsonObject&)> send,
                                     ConfigurationModel initial, QObject* parent)
    : QObject(parent), model_(std::move(initial)), send_(std::move(send)) {}

HostConfiguration::~HostConfiguration() { shutdown(); }

std::shared_ptr<WorkspaceConfiguration> HostConfiguration::getConfiguration(
    const QString& section, const std::optional<ConfigurationScope>& scope) const {
    QMutexLocker lock(&mutex_);
    return std::make_shared<SnapshotConfiguration>(model_, section, scope,
        QPointer<HostConfiguration>(const_cast<HostConfiguration*>(this)));
}

Event<ConfigurationChangeEvent> HostConfiguration::onDidChangeConfiguration() const { return changes_.event(); }

void HostConfiguration::accept(ConfigurationModel model) {
    ConfigurationModel previous;
    ConfigurationModel current;
    {
        QMutexLocker lock(&mutex_);
        if (!accepting_) return;
        previous = model_;
        model_ = std::move(model);
        current = model_;
    }
    if (!current.affectsConfiguration(previous, {})) return;
    changes_.fire(ConfigurationChangeEvent([previous = std::move(previous), current = std::move(current)](
        const QString& section, const std::optional<ConfigurationScope>& scope) {
        return current.affectsConfiguration(previous, section,
            scope ? std::optional<ConfigurationResource>(resource(scope)) : std::nullopt);
    }));
}

QFuture<void> HostConfiguration::requestUpdate(const QString& key, const QJsonValue& value,
    const std::optional<ConfigurationScope>& scope, ConfigurationUpdateTarget target,
    std::optional<bool> overrideInLanguage) {
    auto promise = std::make_shared<QPromise<void>>();
    promise->start();
    const auto future = promise->future();
    quint64 id{};
    {
        QMutexLocker lock(&mutex_);
        if (accepting_) {
            id = ++nextId_;
            pending_.insert(id, promise);
        }
    }
    if (id == 0) { fail(promise, "Extension is shutting down."); return future; }
    QJsonObject message{{"event", "configurationUpdate"}, {"requestId", QString::number(id)},
                        {"key", key}, {"remove", value.isUndefined()}};
    if (!value.isUndefined()) message.insert("value", value);
    if (scope) {
        QJsonObject encoded;
        if (scope->uri) encoded.insert("uri", scope->uri->toString(QUrl::FullyEncoded));
        if (scope->languageId) encoded.insert("languageId", *scope->languageId);
        message.insert("scope", encoded);
    }
    if (const auto* explicitTarget = std::get_if<ConfigurationTarget>(&target))
        message.insert("target", static_cast<int>(*explicitTarget));
    else if (const auto* boolean = std::get_if<bool>(&target)) message.insert("target", *boolean ? 1 : 2);
    if (overrideInLanguage) message.insert("overrideInLanguage", *overrideInLanguage);
    const auto send = [this, id, message] {
        {
            QMutexLocker lock(&mutex_);
            if (!accepting_ || !pending_.contains(id)) return;
        }
        try { send_(message); }
        catch (const std::exception& error) { complete(id, QString::fromUtf8(error.what())); }
        catch (...) { complete(id, "Could not send configuration update request."); }
    };
    if (QThread::currentThread() == thread()) send();
    else if (!QMetaObject::invokeMethod(this, send, Qt::QueuedConnection))
        complete(id, "Could not queue configuration update request.");
    return future;
}

void HostConfiguration::complete(quint64 requestId, const QString& error) {
    std::shared_ptr<QPromise<void>> promise;
    {
        QMutexLocker lock(&mutex_);
        promise = pending_.take(requestId);
    }
    if (!promise) return;
    if (error.isEmpty()) promise->finish(); else fail(promise, error);
}

void HostConfiguration::shutdown() {
    QHash<quint64, std::shared_ptr<QPromise<void>>> pending;
    {
        QMutexLocker lock(&mutex_);
        if (!accepting_) return;
        accepting_ = false;
        pending = std::exchange(pending_, {});
    }
    changes_.dispose();
    for (const auto& promise : pending) fail(promise, "Extension host stopped before the settings update completed.");
}

} // namespace litecode::extensions
