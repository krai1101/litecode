#pragma once

#include "extensions/ConfigurationModel.h"
#include <QMap>

namespace litecode::extensions {

// Workbench-owned contribution metadata. These are private implementation
// types; SDK configuration must use VS Code's ConfigurationTarget and
// WorkspaceConfiguration contracts, not expose this registry as a new API.
enum class SettingScope {
    Application,
    Machine,
    Window,
    Resource,
    LanguageOverridable,
    MachineOverridable
};

struct ConfigurationProperty final {
    QString key, extensionId;
    QJsonObject schema;
    SettingScope scope{SettingScope::Window};
    QJsonValue defaultValue;
};

struct ConfigurationContribution final {
    QString extensionId;
    QJsonObject contributes;
};

class ConfigurationRegistry final {
  public:
    // Rebuilding from enabled extensions makes removal/disable deterministic
    // and also resolves defaults contributed before their setting's owner.
    void replace(const QVector<ConfigurationContribution>& contributions);
    [[nodiscard]] const QMap<QString, ConfigurationProperty>& properties() const {
        return properties_;
    }
    [[nodiscard]] ConfigurationLayer defaults() const;
    [[nodiscard]] QStringList diagnostics(const QString& extensionId) const;
    [[nodiscard]] static QString validateContribution(const QJsonObject& contributes);

  private:
    QMap<QString, ConfigurationProperty> properties_;
    QVector<ConfigurationContribution> contributions_;
    QMap<QString, QStringList> diagnostics_;
};

} // namespace litecode::extensions
