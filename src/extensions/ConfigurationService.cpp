#include "extensions/ConfigurationService.h"
#include "extensions/ConfigurationFile.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <stdexcept>

namespace litecode::extensions {
namespace {

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

bool resourceScoped(SettingScope scope) {
    return scope == SettingScope::Resource || scope == SettingScope::LanguageOverridable ||
           scope == SettingScope::MachineOverridable;
}

} // namespace

ConfigurationService::ConfigurationService(QString globalSettingsFile, QObject* parent)
    : QObject(parent), globalSettingsFile_(QFileInfo(globalSettingsFile).absoluteFilePath()) {
    if (globalSettingsFile.trimmed().isEmpty()) throw std::invalid_argument("Global settings path is empty.");
    debounce_.setSingleShot(true);
    debounce_.setInterval(75);
    connect(&debounce_, &QTimer::timeout, this, &ConfigurationService::refresh);
    const auto schedule = [this](const QString&) { debounce_.start(); };
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, schedule);
    refresh();
}

void ConfigurationService::setWriteGuard(std::function<QString(const QString&)> guard) {
    writeGuard_ = std::move(guard);
}

void ConfigurationService::setRegistry(ConfigurationRegistry registry) {
    registry_ = std::move(registry);
    refresh();
}

void ConfigurationService::setWorkspace(const QString& folder) {
    setWorkspaceFolders(folder.isEmpty() ? QVector<QUrl>{} : QVector<QUrl>{QUrl::fromLocalFile(
        QFileInfo(folder).absoluteFilePath())}, {});
}

void ConfigurationService::setWorkspaceFolders(QVector<QUrl> folders, const QString& workspaceFile) {
    if (workspaceFile.isEmpty() && folders.size() > 1)
        throw std::invalid_argument("Multiple folders require a workspace configuration file.");
    for (const auto& folder : folders)
        if (!folder.isValid() || !folder.isLocalFile())
            throw std::invalid_argument("Configuration folders must be local file URIs.");
    const auto file = workspaceFile.isEmpty() ? QString{} : QFileInfo(workspaceFile).absoluteFilePath();
    if (folders_ == folders && workspaceFile_ == file) return;
    folders_ = std::move(folders);
    workspaceFile_ = file;
    refresh();
}

QString ConfigurationService::folderSettingsFile(int index) const {
    return QDir(folders_.at(index).toLocalFile()).filePath(".vscode/settings.json");
}

ConfigurationLayer ConfigurationService::readLayer(const QString& path, const QStringList& prefix) {
    try {
        auto layer = ConfigurationFile(path, prefix).read();
        lastValidLayers_.insert(path, layer);
        errors_.remove(path);
        return layer;
    } catch (const std::exception& error) {
        const auto message = QString::fromUtf8(error.what());
        if (errors_.value(path) != message) {
            errors_.insert(path, message);
            emit diagnostic(path, message);
        }
        // A half-written settings document must not temporarily replace all
        // effective values with defaults. Writes still reparse and reject it.
        return lastValidLayers_.value(path);
    }
}

ConfigurationLayer ConfigurationService::filterLayer(const ConfigurationLayer& layer, bool folderOnly) const {
    ConfigurationLayer result;
    const auto& properties = registry_.properties();
    for (const auto& key : layer.keys) {
        const auto property = properties.constFind(key);
        if (property != properties.cend() &&
            (property->scope == SettingScope::Application || property->scope == SettingScope::Machine ||
             (folderOnly && !resourceScoped(property->scope)))) continue;
        result.addSetting(key, layer.value(key));
    }
    for (const auto& group : layer.overrides) {
        const ConfigurationLayer values{group.contents, group.keys, {}};
        QJsonObject accepted;
        for (const auto& key : group.keys) {
            const auto property = properties.constFind(key);
            if (property != properties.cend() && property->scope == SettingScope::LanguageOverridable)
                accepted.insert(key, values.value(key));
        }
        if (!accepted.isEmpty()) result.addOverride(group.identifiers, accepted);
    }
    return result;
}

void ConfigurationService::refresh() {
    if (QThread::currentThread() != thread()) fail("Configuration service called outside its workbench thread.");
    ConfigurationModel next;
    try { next.defaults = registry_.defaults(); }
    catch (const std::exception& error) { emit diagnostic({}, QString::fromUtf8(error.what())); }
    const auto rawGlobal = readLayer(globalSettingsFile_);
    next.global = rawGlobal;
    // User settings accept all non-language scopes; language override blocks
    // still require registered language-overridable settings.
    next.global.overrides.clear();
    for (const auto& group : filterLayer(rawGlobal, false).overrides) next.global.overrides.append(group);
    if (!workspaceFile_.isEmpty()) next.workspace = filterLayer(readLayer(workspaceFile_, {"settings"}), false);
    for (int index = 0; index < folders_.size(); ++index) {
        const auto layer = filterLayer(readLayer(folderSettingsFile(index)), !workspaceFile_.isEmpty());
        next.folders.append({folders_.at(index), layer});
        // Upstream maps a single-folder workspace to the same settings model
        // for workspace and folder inspection (not one empty synthetic layer).
        if (workspaceFile_.isEmpty()) next.workspace = layer;
    }
    const bool changed = next.affectsConfiguration(model_, {});
    model_ = std::move(next);
    updateWatchPaths();
    // Bound per-workspace caches: last-valid values are only useful for paths
    // that belong to the current profile/workspace.
    QSet<QString> active{globalSettingsFile_};
    if (!workspaceFile_.isEmpty()) active.insert(workspaceFile_);
    for (int index = 0; index < folders_.size(); ++index) active.insert(folderSettingsFile(index));
    for (auto it = lastValidLayers_.begin(); it != lastValidLayers_.end();)
        if (!active.contains(it.key())) it = lastValidLayers_.erase(it); else ++it;
    for (auto it = errors_.begin(); it != errors_.end();)
        if (!active.contains(it.key())) it = errors_.erase(it); else ++it;
    if (changed) emit this->changed();
}

void ConfigurationService::updateWatchPaths() {
    QSet<QString> files{globalSettingsFile_};
    if (!workspaceFile_.isEmpty()) files.insert(workspaceFile_);
    for (int index = 0; index < folders_.size(); ++index) files.insert(folderSettingsFile(index));
    QSet<QString> paths;
    for (const auto& file : files) {
        if (QFileInfo::exists(file)) paths.insert(file);
        auto directory = QFileInfo(file).absolutePath();
        // Watch an existing ancestor when .vscode or settings.json has not
        // been created yet, then move the watch closer on the next refresh.
        while (!QFileInfo::exists(directory)) {
            const auto parent = QFileInfo(directory).absolutePath();
            if (parent == directory) break;
            directory = parent;
        }
        if (QFileInfo(directory).isDir()) paths.insert(directory);
    }
    const auto watching = watcher_.files() + watcher_.directories();
    for (const auto& path : watching) if (!paths.contains(path)) watcher_.removePath(path);
    for (const auto& path : paths) {
        if (watcher_.files().contains(path) || watcher_.directories().contains(path)) continue;
        if (!watcher_.addPath(path)) emit diagnostic(path, "Could not watch settings path for changes.");
    }
}

void ConfigurationService::update(const QString& key, const QJsonValue& value,
                                   const ConfigurationResource& resource,
                                   std::optional<ConfigurationWriteTarget> target,
                                   std::optional<bool> overrideInLanguage) {
    if (QThread::currentThread() != thread()) fail("Configuration service called outside its workbench thread.");
    const auto property = registry_.properties().constFind(key);
    if (property == registry_.properties().cend()) fail("Configuration is not registered: " + key);
    const auto folder = resource.uri ? model_.folderFor(*resource.uri) : std::nullopt;
    if (!target) target = resourceScoped(property->scope) && folder
        ? ConfigurationWriteTarget::WorkspaceFolder : ConfigurationWriteTarget::Workspace;
    if (*target != ConfigurationWriteTarget::Global && *target != ConfigurationWriteTarget::Workspace &&
        *target != ConfigurationWriteTarget::WorkspaceFolder) fail("Invalid configuration target.");
    if (*target != ConfigurationWriteTarget::Global && folders_.isEmpty() && workspaceFile_.isEmpty())
        fail("Cannot update workspace settings without an open workspace.");
    if (*target != ConfigurationWriteTarget::Global &&
        (property->scope == SettingScope::Application || property->scope == SettingScope::Machine))
        fail("This setting can only be updated in user settings: " + key);
    if (*target == ConfigurationWriteTarget::WorkspaceFolder && (!folder || !resourceScoped(property->scope)))
        fail("Workspace-folder updates require a resource-scoped setting and a URI in that folder.");
    std::optional<QString> language;
    if (overrideInLanguage.value_or(false) && !resource.languageId)
        fail("A language override requires a language-scoped configuration.");
    if (resource.languageId) {
        bool defined = false;
        if (const auto inspection = model_.inspect(key, resource))
            defined = !inspection->defaultLanguageValue.isUndefined() || !inspection->globalLanguageValue.isUndefined() ||
                      !inspection->workspaceLanguageValue.isUndefined() || !inspection->workspaceFolderLanguageValue.isUndefined();
        if (overrideInLanguage.value_or(defined)) language = resource.languageId;
    }
    if (language && property->scope != SettingScope::LanguageOverridable)
        fail("Configuration does not support language overrides: " + key);
    QString path;
    QStringList prefix;
    if (*target == ConfigurationWriteTarget::Global) path = globalSettingsFile_;
    else if (*target == ConfigurationWriteTarget::WorkspaceFolder) path = folderSettingsFile(*folder);
    else if (!workspaceFile_.isEmpty()) { path = workspaceFile_; prefix = {"settings"}; }
    else path = folderSettingsFile(0);
    if (!writeGuard_) fail("Workbench settings editor coordination is not available.");
    const auto guardError = writeGuard_(path);
    if (!guardError.isEmpty()) fail(guardError);
    if (ConfigurationFile(path, prefix).update(key, value, language)) emit fileUpdated(path);
    refresh();
}

} // namespace litecode::extensions
