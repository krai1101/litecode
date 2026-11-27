#pragma once
#include <QFuture>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <utility>

namespace litecode::extensions {

template<typename T> class Event;

enum class ConfigurationTarget { Global = 1, Workspace = 2, WorkspaceFolder = 3 };
// Native URI/language form of the stable ConfigurationScope union. Document
// and WorkspaceFolder conversions accompany those types when implemented.
struct ConfigurationScope final {
    std::optional<QUrl> uri;
    std::optional<QString> languageId;
};
using ConfigurationUpdateTarget = std::variant<std::monostate, ConfigurationTarget, bool>;

struct ConfigurationInspect final {
    QString key;
    QJsonValue defaultValue{QJsonValue::Undefined}, globalValue{QJsonValue::Undefined},
        workspaceValue{QJsonValue::Undefined}, workspaceFolderValue{QJsonValue::Undefined},
        defaultLanguageValue{QJsonValue::Undefined}, globalLanguageValue{QJsonValue::Undefined},
        workspaceLanguageValue{QJsonValue::Undefined}, workspaceFolderLanguageValue{QJsonValue::Undefined};
    std::optional<QStringList> languageIds;
};

class WorkspaceConfiguration {
  public:
    virtual ~WorkspaceConfiguration() = default;
    [[nodiscard]] virtual QJsonValue get(const QString& section) const = 0;
    [[nodiscard]] QJsonValue get(const QString& section, const QJsonValue& defaultValue) const {
        const auto value = get(section);
        return value.isUndefined() ? defaultValue : value;
    }
    [[nodiscard]] virtual bool has(const QString& section) const = 0;
    [[nodiscard]] virtual std::optional<ConfigurationInspect> inspect(const QString& section) const = 0;
    virtual QFuture<void> update(const QString& section, const QJsonValue& value,
        ConfigurationUpdateTarget configurationTarget = {}, std::optional<bool> overrideInLanguage = {}) = 0;
    // Native spelling of the stable configuration object's readonly indexer.
    [[nodiscard]] virtual QJsonValue operator[](const QString& key) const = 0;
};

class ConfigurationChangeEvent final {
  public:
    explicit ConfigurationChangeEvent(std::function<bool(const QString&,
        const std::optional<ConfigurationScope>&)> affects) : affects_(std::move(affects)) {}
    [[nodiscard]] bool affectsConfiguration(const QString& section,
        const std::optional<ConfigurationScope>& scope = {}) const {
        return affects_ && affects_(section, scope);
    }
    [[nodiscard]] bool affectsConfiguration(const QString& section, const QUrl& uri) const {
        return affectsConfiguration(section, ConfigurationScope{uri, {}});
    }
  private:
    std::function<bool(const QString&, const std::optional<ConfigurationScope>&)> affects_;
};

} // namespace litecode::extensions
