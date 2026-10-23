#pragma once

#include "core/DocumentTypes.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

class QSettings;

namespace litecode::core {

struct WindowLayout {
    QByteArray geometry;
    QByteArray state;
    int version{1};
};

struct WorkbenchLayout {
    bool primarySidebarVisible{true};
    int primarySidebarWidth{};
    int primarySidebarView{};
    bool bottomPanelVisible{true};
    int bottomPanelHeight{};
};

class SettingsService final {
  public:
    explicit SettingsService(const QString& fileName = {});
    ~SettingsService();

    SettingsService(const SettingsService&) = delete;
    SettingsService& operator=(const SettingsService&) = delete;

    [[nodiscard]] WindowLayout windowLayout() const;
    void saveWindowLayout(const QByteArray& geometry, const QByteArray& state, int version = 3);
    [[nodiscard]] QString workspaceFolder() const;
    [[nodiscard]] QStringList recentWorkspaceFolders() const;
    [[nodiscard]] QStringList openFiles() const;
    [[nodiscard]] QString activeFile() const;
    void saveSession(const QString& workspaceFolder, const QStringList& openFiles,
                     const QString& activeFile = {});
    void rememberWorkspaceFolder(const QString& workspaceFolder);
    [[nodiscard]] QStringList searchHistory(const QString& workspaceFolder,
                                            const QString& kind) const;
    void saveSearchHistory(const QString& workspaceFolder, const QString& kind,
                           const QStringList& entries);
    [[nodiscard]] bool userEncodingValid() const;
    [[nodiscard]] QString editorFontFamily() const;
    [[nodiscard]] int editorFontSize() const;
    [[nodiscard]] int editorFontWeight() const;
    [[nodiscard]] static QString defaultEditorFontFamily();
    [[nodiscard]] static int defaultEditorFontSize();
    void saveEditorFont(const QString& family, int pixelSize, int weight = 400);
    void saveEditorFontFamily(const QString& family);
    void saveEditorFontSize(int pixelSize);
    void saveEditorFontWeight(int weight);
    void resetEditorFont();
    void resetSetting(const QString& id);
    [[nodiscard]] int editorIndentWidth() const;
    void saveEditorIndentWidth(int width);
    [[nodiscard]] TextEncoding fileEncoding() const;
    void saveFileEncoding(TextEncoding encoding);
    [[nodiscard]] bool fileAutoGuessEncoding() const;
    void saveFileAutoGuessEncoding(bool enabled);
    [[nodiscard]] QString terminalFontFamily() const;
    [[nodiscard]] int terminalFontSize() const;
    void saveTerminalFont(const QString& family, int pointSize);
    [[nodiscard]] QString terminalProfile() const;
    void saveTerminalProfile(const QString& profileId);
    [[nodiscard]] QString commandShortcut(const QString& commandId,
                                          const QString& defaultShortcut) const;
    [[nodiscard]] bool hasCommandShortcut(const QString& commandId) const;
    [[nodiscard]] QHash<QString, QString> commandShortcutOverrides() const;
    void saveCommandShortcut(const QString& commandId, const QString& shortcut);
    void removeCommandShortcut(const QString& commandId);
    [[nodiscard]] QString theme() const;
    void saveTheme(const QString& theme);
    [[nodiscard]] int bottomPanelTab() const;
    void saveBottomPanelTab(int index);
    [[nodiscard]] WorkbenchLayout workbenchLayout() const;
    void saveWorkbenchLayout(const WorkbenchLayout& layout);
    bool sync();

  private:
    std::unique_ptr<QSettings> settings_;
};

} // namespace litecode::core
