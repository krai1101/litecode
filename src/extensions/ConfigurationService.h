#pragma once

#include "extensions/ConfigurationRegistry.h"
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <functional>

namespace litecode::extensions {

// Private workbench controller. Public enum numbering is preserved at the
// transport boundary; this type is not installed with the native SDK.
enum class ConfigurationWriteTarget { Global = 1, Workspace = 2, WorkspaceFolder = 3 };

class ConfigurationService final : public QObject {
    Q_OBJECT
  public:
    explicit ConfigurationService(QString globalSettingsFile, QObject* parent = nullptr);
    void setRegistry(ConfigurationRegistry registry);
    void setWorkspace(const QString& folder);
    void setWorkspaceFolders(QVector<QUrl> folders, const QString& workspaceFile);
    // Workbench must install this guard before SDK updates can write files.
    // A nonempty result rejects the update without touching disk. In
    // particular, a dirty editor must not be overwritten by a disk-only edit.
    void setWriteGuard(std::function<QString(const QString&)> guard);
    [[nodiscard]] const ConfigurationModel& model() const { return model_; }
    [[nodiscard]] const ConfigurationRegistry& registry() const { return registry_; }
    void refresh();
    void update(const QString& key, const QJsonValue& value,
                const ConfigurationResource& resource = {},
                std::optional<ConfigurationWriteTarget> target = {},
                std::optional<bool> overrideInLanguage = {});

  signals:
    void changed();
    void diagnostic(const QString& path, const QString& error);
    void fileUpdated(const QString& path);

  private:
    [[nodiscard]] QString folderSettingsFile(int index) const;
    [[nodiscard]] ConfigurationLayer readLayer(const QString& path, const QStringList& prefix = {});
    [[nodiscard]] ConfigurationLayer filterLayer(const ConfigurationLayer& layer, bool folderOnly) const;
    void updateWatchPaths();
    QString globalSettingsFile_, workspaceFile_;
    QVector<QUrl> folders_;
    ConfigurationRegistry registry_;
    ConfigurationModel model_;
    QHash<QString, ConfigurationLayer> lastValidLayers_;
    QHash<QString, QString> errors_;
    std::function<QString(const QString&)> writeGuard_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
};

} // namespace litecode::extensions
