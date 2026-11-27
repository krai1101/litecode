#pragma once

#include "extensions/ExtensionApi.h"
#include "extensions/ConfigurationModel.h"
#include <QHash>
#include <QMutex>
#include <QPromise>

namespace litecode::extensions {

// Separate host-side API bridge. It never opens settings files, runs schema
// registration, or writes into the workbench's editor documents.
class HostConfiguration final : public QObject {
  public:
    explicit HostConfiguration(std::function<void(const QJsonObject&)> send,
                                ConfigurationModel initial, QObject* parent = nullptr);
    ~HostConfiguration() override;
    [[nodiscard]] std::shared_ptr<WorkspaceConfiguration> getConfiguration(
        const QString& section = {}, const std::optional<ConfigurationScope>& scope = {}) const;
    [[nodiscard]] Event<ConfigurationChangeEvent> onDidChangeConfiguration() const;
    void accept(ConfigurationModel model);
    void complete(quint64 requestId, const QString& error);
    void shutdown();
    QFuture<void> requestUpdate(const QString& key, const QJsonValue& value,
        const std::optional<ConfigurationScope>& scope, ConfigurationUpdateTarget target,
        std::optional<bool> overrideInLanguage);

  private:
    mutable QMutex mutex_;
    ConfigurationModel model_;
    std::function<void(const QJsonObject&)> send_;
    EventEmitter<ConfigurationChangeEvent> changes_;
    QHash<quint64, std::shared_ptr<QPromise<void>>> pending_;
    quint64 nextId_{};
    bool accepting_{true};
};

} // namespace litecode::extensions
