#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <array>
#include <optional>

namespace litecode::extensions {

[[nodiscard]] QStringList configurationOverrideIdentifiers(const QString& key);

// Private configuration engine, not a replacement public SDK API. Settings
// parsing, persistence, schema registration and the host bridge are separate
// concerns. Snapshots are value types so previously returned configurations
// and change events cannot change underneath an extension.
struct ConfigurationResource final {
    std::optional<QUrl> uri;
    std::optional<QString> languageId;
};

struct ConfigurationOverride final {
    QStringList identifiers;
    QJsonObject contents;
    QStringList keys;
};

struct ConfigurationLayer final {
    QJsonObject contents;
    QStringList keys;
    QVector<ConfigurationOverride> overrides;

    // Retain source ordering of override groups: a parser must append these in
    // document order instead of routing them through a sorted QJsonObject.
    void addSetting(const QString& key, const QJsonValue& value);
    void addOverride(QStringList identifiers, const QJsonObject& settings);
    [[nodiscard]] QJsonValue value(const QString& section) const;
    [[nodiscard]] QJsonValue languageValue(const QString& section,
                                            const QString& languageId) const;
    [[nodiscard]] QStringList languageIds(const QString& section) const;
    [[nodiscard]] ConfigurationLayer merged(const ConfigurationLayer& other) const;
};

struct ConfigurationFolder final {
    QUrl uri;
    ConfigurationLayer settings;
};

struct ConfigurationInspection final {
    QString key;
    QJsonValue defaultValue{QJsonValue::Undefined};
    QJsonValue globalValue{QJsonValue::Undefined};
    QJsonValue workspaceValue{QJsonValue::Undefined};
    QJsonValue workspaceFolderValue{QJsonValue::Undefined};
    QJsonValue defaultLanguageValue{QJsonValue::Undefined};
    QJsonValue globalLanguageValue{QJsonValue::Undefined};
    QJsonValue workspaceLanguageValue{QJsonValue::Undefined};
    QJsonValue workspaceFolderLanguageValue{QJsonValue::Undefined};
    QStringList languageIds;
};

class ConfigurationModel final {
  public:
    ConfigurationLayer defaults, global, workspace;
    QVector<ConfigurationFolder> folders;

    [[nodiscard]] QJsonValue get(const QString& section,
                                const ConfigurationResource& resource = {}) const;
    [[nodiscard]] bool has(const QString& section,
                          const ConfigurationResource& resource = {}) const;
    [[nodiscard]] std::optional<ConfigurationInspection> inspect(
        const QString& section, const ConfigurationResource& resource = {}) const;
    [[nodiscard]] std::optional<int> folderFor(const QUrl& resource) const;
    // Without a resource, this checks changes in any folder or language,
    // including a lower-priority layer masked by another layer. With a
    // resource it checks the effective scoped value, like VS Code's event.
    [[nodiscard]] bool affectsConfiguration(
        const ConfigurationModel& previous, const QString& section,
        const std::optional<ConfigurationResource>& resource = {}) const;

  private:
    [[nodiscard]] std::array<const ConfigurationLayer*, 4> layers(
        const ConfigurationResource& resource) const;
};

} // namespace litecode::extensions
