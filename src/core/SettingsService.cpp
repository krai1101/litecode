#include "core/SettingsService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace litecode::core {
namespace {

constexpr int normalFontWeight = 400;
constexpr int semiBoldFontWeight = 600;
constexpr int boldFontWeight = 700;

std::optional<TextEncoding> validEncoding(const QVariant& stored) {
    if (!stored.isValid())
        return std::nullopt;

    if (const std::optional<TextEncoding> encoding = textEncodingFromKey(stored.toString()))
        return encoding;

    bool converted = false;
    const int value = stored.toInt(&converted);
    if (!converted)
        return std::nullopt;
    for (const TextEncoding encoding : supportedTextEncodings()) {
        if (value == static_cast<int>(encoding))
            return encoding;
    }
    return std::nullopt;
}

} // namespace

SettingsService::SettingsService(const QString& fileName)
    : settings_(fileName.isEmpty() ? std::make_unique<QSettings>()
                                   : std::make_unique<QSettings>(fileName, QSettings::IniFormat)) {}

SettingsService::~SettingsService() = default;
GitPreferences SettingsService::gitPreferences() const {
    GitPreferences result;
    result.branchPrefix = settings_->value("git/branchPrefix").toString();
    result.branchWhitespace = settings_->value("git/branchWhitespace", "-").toString();
    result.randomBranchName = settings_->value("git/randomBranchName", false).toBool();
    result.pruneOnFetch = settings_->value("git/pruneOnFetch", false).toBool();
    result.fetchOnPull = settings_->value("git/fetchOnPull", false).toBool();
    result.pullTags = settings_->value("git/pullTags", true).toBool();
    result.autoStash = settings_->value("git/autoStash", false).toBool();
    result.allowNoVerifyCommit = settings_->value("git/allowNoVerifyCommit", false).toBool();
    result.allowForcePush = settings_->value("git/allowForcePush", false).toBool();
    result.useForcePushWithLease = settings_->value("git/useForcePushWithLease", true).toBool();
    result.useForcePushIfIncludes = settings_->value("git/useForcePushIfIncludes", true).toBool();
    result.confirmForcePush = settings_->value("git/confirmForcePush", true).toBool();
    const int policy = settings_->value("git/pullPolicy", 0).toInt();
    if (policy >= 0 && policy <= 3)
        result.pullPolicy = static_cast<GitPullPolicy>(policy);
    return result;
}
void SettingsService::saveGitPreferences(const GitPreferences& preferences) {
    settings_->setValue("git/branchPrefix", preferences.branchPrefix);
    settings_->setValue("git/branchWhitespace", preferences.branchWhitespace);
    settings_->setValue("git/randomBranchName", preferences.randomBranchName);
    settings_->setValue("git/pruneOnFetch", preferences.pruneOnFetch);
    settings_->setValue("git/fetchOnPull", preferences.fetchOnPull);
    settings_->setValue("git/pullTags", preferences.pullTags);
    settings_->setValue("git/autoStash", preferences.autoStash);
    settings_->setValue("git/allowNoVerifyCommit", preferences.allowNoVerifyCommit);
    settings_->setValue("git/allowForcePush", preferences.allowForcePush);
    settings_->setValue("git/useForcePushWithLease", preferences.useForcePushWithLease);
    settings_->setValue("git/useForcePushIfIncludes", preferences.useForcePushIfIncludes);
    settings_->setValue("git/confirmForcePush", preferences.confirmForcePush);
    settings_->setValue("git/pullPolicy", static_cast<int>(preferences.pullPolicy));
}

bool SettingsService::userEncodingValid() const {
    const QVariant stored = settings_->value(QStringLiteral("files/encoding"));
    return !stored.isValid() || validEncoding(stored).has_value();
}

WindowLayout SettingsService::windowLayout() const {
    settings_->beginGroup(QStringLiteral("mainWindow"));
    const WindowLayout layout{
        settings_->value(QStringLiteral("geometry")).toByteArray(),
        settings_->value(QStringLiteral("state")).toByteArray(),
        settings_->value(QStringLiteral("version"), 1).toInt(),
    };
    settings_->endGroup();
    return layout;
}

void SettingsService::saveWindowLayout(const QByteArray& geometry, const QByteArray& state,
                                       int version) {
    settings_->beginGroup(QStringLiteral("mainWindow"));
    settings_->setValue(QStringLiteral("geometry"), geometry);
    settings_->setValue(QStringLiteral("state"), state);
    settings_->setValue(QStringLiteral("version"), version);
    settings_->endGroup();
}

QString SettingsService::workspaceFolder() const {
    return settings_->value(QStringLiteral("session/workspaceFolder")).toString();
}

QStringList SettingsService::recentWorkspaceFolders() const {
    QStringList result;
    const QStringList stored =
        settings_->value(QStringLiteral("session/recentWorkspaceFolders")).toStringList();
    for (const QString& path : stored) {
        const QString cleanPath = QDir::cleanPath(path);
        const QFileInfo folder(cleanPath);
        if (folder.isAbsolute() && folder.isDir() && !result.contains(cleanPath))
            result.append(cleanPath);
    }
    const QString current = workspaceFolder();
    if (!settings_->contains(QStringLiteral("session/recentWorkspaceFolders")) &&
        QFileInfo(current).isAbsolute() && QFileInfo(current).isDir())
        result.prepend(current);
    return result;
}

QStringList SettingsService::openFiles() const {
    return settings_->value(QStringLiteral("session/openFiles")).toStringList();
}

QString SettingsService::activeFile() const {
    return settings_->value(QStringLiteral("session/activeFile")).toString();
}

void SettingsService::saveSession(const QString& workspaceFolder, const QStringList& openFiles,
                                  const QString& activeFile) {
    // A failed or interrupted startup must not erase the last usable workspace. LiteCode does not
    // currently expose an explicit "Close Workspace" command, so an empty controller path means
    // there is nothing new to persist rather than a request to forget the previous folder.
    if (workspaceFolder.isEmpty() && openFiles.isEmpty() && activeFile.isEmpty())
        return;
    if (!workspaceFolder.isEmpty())
        settings_->setValue(QStringLiteral("session/workspaceFolder"), workspaceFolder);
    settings_->setValue(QStringLiteral("session/openFiles"), openFiles);
    settings_->setValue(QStringLiteral("session/activeFile"), activeFile);
}

void SettingsService::rememberWorkspaceFolder(const QString& workspaceFolder) {
    const QString cleanPath = QDir::cleanPath(workspaceFolder);
    if (!QDir::isAbsolutePath(cleanPath))
        return;
    QStringList recent = recentWorkspaceFolders();
    recent.removeAll(cleanPath);
    recent.prepend(cleanPath);
    constexpr qsizetype maximumRecentWorkspaces = 10;
    if (recent.size() > maximumRecentWorkspaces)
        recent.erase(recent.begin() + maximumRecentWorkspaces, recent.end());
    settings_->setValue(QStringLiteral("session/recentWorkspaceFolders"), recent);
}

QStringList SettingsService::recentFiles() const {
    auto paths = settings_->value(QStringLiteral("session/recentFiles"), openFiles()).toStringList();
    paths.removeIf([](const QString& path) {
        const QFileInfo file(path);
        return !file.isAbsolute() || !file.isFile();
    });
    paths.removeDuplicates();
    return paths;
}

void SettingsService::rememberRecentFile(const QString& path) {
    const QFileInfo file(path);
    if (!file.isAbsolute() || !file.isFile())
        return;
    auto paths = recentFiles();
    paths.removeAll(file.absoluteFilePath());
    paths.prepend(file.absoluteFilePath());
    settings_->setValue(QStringLiteral("session/recentFiles"), paths.mid(0, 10));
}

void SettingsService::forgetRecentPath(const QString& path) {
    auto folders = recentWorkspaceFolders();
    if (folders.removeAll(path))
        settings_->setValue(QStringLiteral("session/recentWorkspaceFolders"), folders);
    auto files = recentFiles();
    if (files.removeAll(path))
        settings_->setValue(QStringLiteral("session/recentFiles"), files);
}

QStringList SettingsService::searchHistory(const QString& workspaceFolder,
                                           const QString& kind) const {
    const QString scope =
        QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(workspaceFolder).toUtf8(),
                                                     QCryptographicHash::Sha256)
                                .toHex());
    return settings_->value(QStringLiteral("history/%1/%2").arg(scope, kind)).toStringList();
}

void SettingsService::saveSearchHistory(const QString& workspaceFolder, const QString& kind,
                                        const QStringList& entries) {
    const QString scope =
        QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(workspaceFolder).toUtf8(),
                                                     QCryptographicHash::Sha256)
                                .toHex());
    settings_->setValue(QStringLiteral("history/%1/%2").arg(scope, kind), entries.mid(0, 50));
}

QMap<QString, QSet<int>> SettingsService::debugBreakpoints(const QString& workspaceFolder) const {
    QMap<QString, QSet<int>> result;
    if (workspaceFolder.isEmpty()) return result;
    const QString scope = QString::fromLatin1(
        QCryptographicHash::hash(QDir::cleanPath(workspaceFolder).toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
    const auto document = QJsonDocument::fromJson(
        settings_->value(QStringLiteral("debug/%1/breakpoints").arg(scope)).toByteArray());
    const auto stored = document.object();
    for (auto it = stored.begin(); it != stored.end(); ++it) {
        if (!QDir::isAbsolutePath(it.key()) || !it.value().isArray()) continue;
        QSet<int> lines;
        for (const auto& value : it.value().toArray()) {
            const int line = value.toInt();
            if (line > 0) lines.insert(line);
        }
        if (!lines.isEmpty()) result.insert(QDir::cleanPath(it.key()), lines);
    }
    return result;
}
void SettingsService::saveDebugBreakpoints(
    const QString& workspaceFolder, const QMap<QString, QSet<int>>& breakpoints) {
    if (workspaceFolder.isEmpty()) return;
    const QString scope = QString::fromLatin1(
        QCryptographicHash::hash(QDir::cleanPath(workspaceFolder).toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
    QJsonObject values;
    for (auto it = breakpoints.cbegin(); it != breakpoints.cend(); ++it) {
        QJsonArray lines;
        for (int line : it.value())
            if (line > 0) lines.append(line);
        if (!lines.isEmpty()) values.insert(it.key(), lines);
    }
    const QString key = QStringLiteral("debug/%1/breakpoints").arg(scope);
    if (values.isEmpty()) settings_->remove(key);
    else settings_->setValue(key, QJsonDocument(values).toJson(QJsonDocument::Compact));
}

QString SettingsService::editorFontFamily() const {
    const QVariant stored = settings_->value(QStringLiteral("editor/fontFamily"));
    const QString family = stored.toString().trimmed();
    if (!family.isEmpty())
        return family;
    return defaultEditorFontFamily();
}

int SettingsService::editorFontSize() const {
    const QVariant stored = settings_->value(QStringLiteral("editor/fontSize"));
    if (!stored.isValid())
        return defaultEditorFontSize();

    bool validSize = false;
    const int size = stored.toInt(&validSize);
    if (!validSize || size <= 0) {
        return defaultEditorFontSize();
    }
    const QString unit = settings_->value(QStringLiteral("editor/fontSizeUnit")).toString();
    // Values written before 0.1.0 were Qt points. Preserve their approximate visual size
    // while moving the user-facing setting to the VS Code-compatible pixel contract.
    if (unit != QStringLiteral("px")) {
        return qBound(6, qRound(static_cast<double>(size) * 96.0 / 72.0), 72);
    }
    return qBound(6, size, 72);
}

int SettingsService::editorFontWeight() const {
    const QVariant value = settings_->value(QStringLiteral("editor/fontWeight"));
    const int stored = value.isValid() ? value.toInt() : normalFontWeight;
    if (stored == semiBoldFontWeight || stored == boldFontWeight)
        return stored;
    return normalFontWeight;
}

QString SettingsService::defaultEditorFontFamily() {
#ifdef Q_OS_MACOS
    return QStringLiteral("Menlo");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Consolas");
#else
    return QStringLiteral("monospace");
#endif
}

int SettingsService::defaultEditorFontSize() {
#ifdef Q_OS_MACOS
    return 12;
#else
    return 14;
#endif
}

void SettingsService::saveEditorFont(const QString& family, int pixelSize, int weight) {
    saveEditorFontFamily(family);
    saveEditorFontSize(pixelSize);
    saveEditorFontWeight(weight);
}

void SettingsService::saveEditorFontFamily(const QString& family) {
    settings_->setValue(QStringLiteral("editor/fontFamily"), family);
}

void SettingsService::saveEditorFontSize(int pixelSize) {
    settings_->setValue(QStringLiteral("editor/fontSize"), qBound(6, pixelSize, 72));
    settings_->setValue(QStringLiteral("editor/fontSizeUnit"), QStringLiteral("px"));
}

void SettingsService::saveEditorFontWeight(int weight) {
    settings_->setValue(
        QStringLiteral("editor/fontWeight"),
        weight == semiBoldFontWeight || weight == boldFontWeight ? weight : normalFontWeight);
}

void SettingsService::resetEditorFont() {
    settings_->remove(QStringLiteral("editor/fontFamily"));
    settings_->remove(QStringLiteral("editor/fontSize"));
    settings_->remove(QStringLiteral("editor/fontSizeUnit"));
    settings_->remove(QStringLiteral("editor/fontWeight"));
}

void SettingsService::resetSetting(const QString& id) {
    QStringList keys;
    if (id == QStringLiteral("editor.fontFamily"))
        keys = {QStringLiteral("editor/fontFamily")};
    else if (id == QStringLiteral("editor.fontWeight"))
        keys = {QStringLiteral("editor/fontWeight")};
    else if (id == QStringLiteral("editor.fontSize"))
        keys = {QStringLiteral("editor/fontSize"), QStringLiteral("editor/fontSizeUnit")};
    else if (id == QStringLiteral("files.autoGuessEncoding"))
        keys = {QStringLiteral("files/autoGuessEncoding")};
    else if (id == QStringLiteral("files.encoding"))
        keys = {QStringLiteral("files/encoding")};
    if (keys.isEmpty())
        return;
    bool hasOverride = false;
    for (const QString& key : keys)
        hasOverride = hasOverride || settings_->contains(key);
    if (!hasOverride)
        return;
    for (const QString& key : keys)
        settings_->remove(key);
}

int SettingsService::editorIndentWidth() const {
    return qBound(1, settings_->value(QStringLiteral("editor/indentWidth"), 4).toInt(), 16);
}

void SettingsService::saveEditorIndentWidth(int width) {
    settings_->setValue(QStringLiteral("editor/indentWidth"), qBound(1, width, 16));
}

TextEncoding SettingsService::fileEncoding() const {
    if (const auto encoding = validEncoding(settings_->value(QStringLiteral("files/encoding"))))
        return *encoding;
    return TextEncoding::Utf8;
}

void SettingsService::saveFileEncoding(TextEncoding encoding) {
    settings_->setValue(QStringLiteral("files/encoding"), textEncodingKey(encoding));
}

bool SettingsService::fileAutoGuessEncoding() const {
    return settings_->value(QStringLiteral("files/autoGuessEncoding")).toBool();
}

void SettingsService::saveFileAutoGuessEncoding(bool enabled) {
    settings_->setValue(QStringLiteral("files/autoGuessEncoding"), enabled);
}

QString SettingsService::terminalFontFamily() const {
    return settings_->value(QStringLiteral("terminal/fontFamily"), QStringLiteral("Consolas"))
        .toString();
}

int SettingsService::terminalFontSize() const {
    return qBound(6, settings_->value(QStringLiteral("terminal/fontSize"), 11).toInt(), 32);
}

void SettingsService::saveTerminalFont(const QString& family, int pointSize) {
    settings_->setValue(QStringLiteral("terminal/fontFamily"), family);
    settings_->setValue(QStringLiteral("terminal/fontSize"), qBound(6, pointSize, 32));
}

QString SettingsService::terminalProfile() const {
    return settings_->value(QStringLiteral("terminal/defaultProfile")).toString();
}

void SettingsService::saveTerminalProfile(const QString& profileId) {
    settings_->setValue(QStringLiteral("terminal/defaultProfile"), profileId);
}

QString SettingsService::commandShortcut(const QString& commandId,
                                         const QString& defaultShortcut) const {
    return settings_->value(QStringLiteral("shortcuts/") + commandId, defaultShortcut).toString();
}

bool SettingsService::hasCommandShortcut(const QString& commandId) const {
    return settings_->contains(QStringLiteral("shortcuts/") + commandId);
}

QHash<QString, QString> SettingsService::commandShortcutOverrides() const {
    QHash<QString, QString> overrides;
    settings_->beginGroup(QStringLiteral("shortcuts"));
    for (const QString& key : settings_->childKeys())
        overrides.insert(key, settings_->value(key).toString());
    settings_->endGroup();
    return overrides;
}

void SettingsService::saveCommandShortcut(const QString& commandId, const QString& shortcut) {
    settings_->setValue(QStringLiteral("shortcuts/") + commandId, shortcut);
}

void SettingsService::removeCommandShortcut(const QString& commandId) {
    settings_->remove(QStringLiteral("shortcuts/") + commandId);
}

QString SettingsService::theme() const {
    return settings_->value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString();
}

void SettingsService::saveTheme(const QString& theme) {
    settings_->setValue(QStringLiteral("appearance/theme"), theme);
}

int SettingsService::bottomPanelTab() const {
    return settings_->value(QStringLiteral("workbench/bottomPanelTab"), 0).toInt();
}

void SettingsService::saveBottomPanelTab(int index) {
    settings_->setValue(QStringLiteral("workbench/bottomPanelTab"), qMax(0, index));
}

WorkbenchLayout SettingsService::workbenchLayout() const {
    return {
        settings_->value(QStringLiteral("workbench/primarySidebarVisible"), true).toBool(),
        settings_->value(QStringLiteral("workbench/primarySidebarWidth"), 0).toInt(),
        settings_->value(QStringLiteral("workbench/primarySidebarView"), 0).toInt(),
        settings_->value(QStringLiteral("workbench/bottomPanelVisible"), true).toBool(),
        settings_->value(QStringLiteral("workbench/bottomPanelHeight"), 0).toInt(),
    };
}

void SettingsService::saveWorkbenchLayout(const WorkbenchLayout& layout) {
    settings_->setValue(QStringLiteral("workbench/primarySidebarVisible"),
                        layout.primarySidebarVisible);
    settings_->setValue(QStringLiteral("workbench/primarySidebarWidth"),
                        layout.primarySidebarWidth);
    settings_->setValue(QStringLiteral("workbench/primarySidebarView"), layout.primarySidebarView);
    settings_->setValue(QStringLiteral("workbench/bottomPanelVisible"), layout.bottomPanelVisible);
    settings_->setValue(QStringLiteral("workbench/bottomPanelHeight"), layout.bottomPanelHeight);
}

bool SettingsService::sync() {
    settings_->sync();
    return settings_->status() == QSettings::NoError;
}

QByteArray SettingsService::sidebarState() const {
    return settings_->value(QStringLiteral("workbench/sidebarContainers")).toByteArray();
}
void SettingsService::saveSidebarState(const QByteArray& state) {
    settings_->setValue(QStringLiteral("workbench/sidebarContainers"), state);
}

} // namespace litecode::core
