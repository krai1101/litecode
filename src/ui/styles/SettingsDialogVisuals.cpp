#include "ui/styles/SettingsDialogVisuals.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QDialog>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace litecode::ui {

void applySettingsDialogVisuals(QDialog& dialog) {
    const bool darkTheme = qApp->property("litecodeDarkTheme").toBool();
    const ThemeTokens tokens = Theme::tokens(darkTheme);
    const bool settings = dialog.objectName() == QStringLiteral("settingsDialog");

    QPalette dialogPalette = dialog.palette();
    dialogPalette.setColor(QPalette::Window,
                           settings ? tokens.settingsSurface : tokens.elevatedSurface);
    dialogPalette.setColor(QPalette::WindowText, tokens.foreground);
    dialogPalette.setColor(QPalette::Base, settings ? tokens.dropdownSurface : tokens.inputSurface);
    dialogPalette.setColor(QPalette::AlternateBase, tokens.hover);
    dialogPalette.setColor(QPalette::Text, tokens.foreground);
    dialogPalette.setColor(QPalette::Button, tokens.inputSurface);
    dialogPalette.setColor(QPalette::ButtonText, tokens.foreground);
    dialogPalette.setColor(QPalette::Highlight, tokens.selection);
    dialogPalette.setColor(QPalette::HighlightedText, tokens.foreground);
    dialogPalette.setColor(QPalette::Disabled, QPalette::Text, tokens.disabledForeground);
    dialogPalette.setColor(QPalette::Disabled, QPalette::ButtonText, tokens.disabledForeground);

    dialog.setPalette(dialogPalette);
    for (QWidget* child : dialog.findChildren<QWidget*>())
        child->setPalette(dialogPalette);

    // LiteCode dialogs use shared custom chrome and do not need a separate
    // application glyph. Keep Qt from supplying its generic fallback icon.
    QPixmap transparentIcon(16, 16);
    transparentIcon.fill(Qt::transparent);
    dialog.setWindowIcon(QIcon(transparentIcon));

#ifdef Q_OS_WIN
    // QSS cannot theme a native Windows title bar.
    using DwmSetWindowAttributeFunction = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE library = LoadLibraryW(L"dwmapi.dll")) {
        const auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFunction>(
            GetProcAddress(library, "DwmSetWindowAttribute"));
        if (setWindowAttribute != nullptr) {
            constexpr DWORD useImmersiveDarkMode = 20;
            const BOOL enabled = darkTheme ? TRUE : FALSE;
            setWindowAttribute(reinterpret_cast<HWND>(dialog.winId()), useImmersiveDarkMode,
                               &enabled, sizeof(enabled));
        }
        FreeLibrary(library);
    }
#endif
}

} // namespace litecode::ui
