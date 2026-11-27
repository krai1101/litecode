#include "ui/Theme.h"

#include "ui/components/ComponentStyle.h"
#include "ui/components/ComponentTokens.h"
#include "ui/components/Tooltip.h"
#include "ui/styles/ComponentStyleSheets.h"

#include <QApplication>
#include <QFont>
#include <QPalette>

namespace litecode::ui {

void Theme::apply(QApplication& application, bool dark) {
    application.setProperty("litecodeDarkTheme", dark);
    QFont interfaceFont(QStringLiteral("Segoe UI"));
    interfaceFont.setPixelSize(13);
    application.setFont(interfaceFont);
    const ThemeTokens theme = tokens(dark);
    QPalette palette;
    palette.setColor(QPalette::Window, theme.appSurface);
    palette.setColor(QPalette::WindowText, theme.foreground);
    palette.setColor(QPalette::Base, theme.inputSurface);
    palette.setColor(QPalette::AlternateBase, theme.sidebarSurface);
    palette.setColor(QPalette::Text, theme.foreground);
    palette.setColor(QPalette::Button, theme.inputSurface);
    palette.setColor(QPalette::ButtonText, theme.foreground);
    palette.setColor(QPalette::ToolTipBase, QColor(dark ? "#333333" : "#fafafa"));
    palette.setColor(QPalette::ToolTipText, QColor(dark ? "#f5f5f5" : "#3b3b3b"));
    palette.setColor(QPalette::PlaceholderText, theme.placeholderForeground);
    palette.setColor(QPalette::Highlight, theme.listSelectionBackground);
    palette.setColor(QPalette::HighlightedText, theme.listSelectionForeground);
    palette.setColor(QPalette::Link, dark ? QColor("#4daafc") : theme.accent);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, theme.disabledForeground);
    application.setPalette(palette);
    application.setStyleSheet(dark ? darkStyleSheet() : lightStyleSheet());
    components::installTooltips(application);
}

WorkbenchPalette Theme::workbenchPalette(bool dark) {
    const ThemeTokens theme = tokens(dark);
    return {theme.sidebarSurface, theme.border};
}

ThemeTokens Theme::tokens(bool dark) {
    // Sources: microsoft/vscode extensions/theme-defaults/themes/{light,dark}_modern.json.
    // Inherited roles: src/vs/platform/theme/common/colors/{base,misc,list,menu}Colors.ts.
    // Each assignment names its role; no positional palette or post-hoc overrides.
    const auto color = [dark](const char* light, const char* darkValue) {
        return QColor(QString::fromLatin1(dark ? darkValue : light));
    };
    const auto alpha = [](QColor value, int opacity) {
        value.setAlpha(opacity);
        return value;
    };
    ThemeTokens theme;
    theme.appSurface = color("#f8f8f8", "#181818");          // titleBar.activeBackground
    theme.editorSurface = color("#ffffff", "#1f1f1f");       // editor.background
    theme.sidebarSurface = theme.appSurface;                 // sideBar.background
    theme.panelSurface = theme.appSurface;                   // panel.background
    theme.elevatedSurface = color("#f8f8f8", "#222222");     // quickInput.background
    theme.foreground = color("#3b3b3b", "#cccccc");          // foreground
    theme.mutedForeground = color("#3b3b3b", "#9d9d9d");     // descriptionForeground
    theme.border = color("#e5e5e5", "#2b2b2b");              // sideBar.border
    theme.shadow = alpha(QColor("#000000"), dark ? 92 : 41); // widget.shadow
    theme.hover = color("#f2f2f2", "#2a2d2e");               // list.hoverBackground
    theme.listHoverBackground = theme.hover;
    theme.listSelectionBackground = color("#e8e8e8", "#04395e"); // list.activeSelectionBackground
    theme.listSelectionForeground = color("#000000", "#ffffff");
    theme.selection = theme.listSelectionBackground;
    theme.focus = color("#005fb8", "#0078d4"); // focusBorder
    theme.accent = theme.focus;
    theme.error = QColor("#f85149");             // errorForeground
    theme.warning = color("#895503", "#e2c08d"); // gitDecoration.modifiedResourceForeground
    theme.success = QColor("#2ea043");           // editorGutter.addedBackground
    theme.scrollbar = alpha(color("#646464", "#797979"), 102);
    theme.scrollbarHover = alpha(QColor("#646464"), 179);
    theme.scrollbarActive = alpha(color("#000000", "#bfbfbf"), dark ? 102 : 153);
    theme.inputSurface = color("#ffffff", "#313131"); // input.background
    theme.inputBorder = color("#cecece", "#3c3c3c");  // input.border
    theme.pressed = theme.hover;
    // Qt disabled button surfaces have no direct CSS-opacity equivalent.
    theme.disabledSurface = color("#dddddd", "#313131");
    theme.disabledForeground = alpha(color("#616161", "#cccccc"), 128); // disabledForeground
    theme.onAccent = QColor("#ffffff");
    theme.closeHover = QColor("#c42b1c"); // destructive/native caption control
    theme.settingsSurface = theme.editorSurface;
    theme.settingsHeaderForeground = color("#1f1f1f", "#ffffff");
    theme.dropdownSurface = theme.inputSurface;
    theme.dropdownBorder = theme.inputBorder;
    theme.dropdownListSurface = theme.editorSurface;
    theme.dropdownListBorder = theme.inputBorder;
    theme.dropdownForeground = theme.foreground;
    theme.findWidgetSurface = color("#f8f8f8", "#202020"); // editorWidget.background
    theme.findOptionActiveBackground = dark ? QColor(36, 137, 219, 130) : QColor("#bed6ed");
    theme.findOptionActiveBorder = color("#005fb8", "#2488db");
    theme.findOptionActiveForeground = color("#000000", "#ffffff"); // inputOption.activeForeground
    theme.menuSurface = theme.editorSurface; // menu.background -> dropdown.listBackground
    theme.menuBorder = dark ? QColor(Qt::transparent) : QColor("#cecece"); // menu.border
    theme.menuArrowIcon = dark ? QStringLiteral(":/icons/chevron-right-menu-dark.svg")
                               : QStringLiteral(":/icons/chevron-right-menu-light.svg");
    theme.titleBarForeground = color("#1e1e1e", "#cccccc");
    theme.titleBarControlHover = alpha(color("#000000", "#ffffff"), 26);
    theme.titleBarCloseHover = QColor(232, 17, 35, 230); // native Windows caption convention
    theme.searchFocusBorder = theme.focus;
    theme.iconForeground = theme.foreground;
    theme.terminalSelectionBackground = color("#add6ff", "#264f78");
    theme.terminalSearchBackground =
        QColor(234, 92, 0, 85); // terminal.findMatchHighlightBackground
    theme.terminalCurrentSearchBackground = color("#a8ac94", "#9e6a03");
    theme.terminalSessionActionHover = theme.hover;
    theme.windowOutline = theme.border;
    theme.popupShadow = theme.shadow;
    theme.inactiveTabForeground = color("#616161", "#9d9d9d");
    theme.activeTabForeground = color("#3b3b3b", "#ffffff");
    theme.placeholderForeground = color("#767676", "#989898");
    theme.primaryButtonHover = color("#0258a8", "#026ec1");
    // Explicit user-requested exception: neutral popup hover, not VS Code's blue menu selection.
    theme.menuSelectionBackground = theme.hover;
    theme.menuSelectionForeground = theme.foreground;
    theme.activityForeground = color("#1f1f1f", "#d7d7d7");
    theme.activityInactiveForeground = color("#616161", "#868686");
    theme.badgeBackground = color("#cccccc", "#616161");
    theme.badgeForeground = color("#3b3b3b", "#f8f8f8");
    theme.secondaryButtonBackground = dark ? QColor(Qt::transparent) : QColor("#e5e5e5");
    theme.secondaryButtonHover = color("#cccccc", "#2b2b2b");
    theme.textSelectionBackground = theme.terminalSelectionBackground;
    // VS Code Git extension's gitDecoration.*ResourceForeground defaults.
    theme.gitAdded = color("#587c0c", "#81b88b");
    theme.gitModified = color("#895503", "#e2c08d");
    theme.gitDeleted = color("#ad0707", "#c74e39");
    theme.gitUntracked = color("#007100", "#73c991");
    theme.gitRenamed = theme.gitUntracked;
    theme.gitConflict = color("#ad0707", "#e4676b");
    return theme;
}

namespace {

QString composeStyleSheet(const ThemeTokens& tokens) {
    return components::componentStyleSheet(components::resolveComponentTokens(tokens)) +
           ComponentStyleSheets::workbench(tokens) + ComponentStyleSheets::explorer(tokens) +
           ComponentStyleSheets::editorChrome(tokens) +
           ComponentStyleSheets::terminalAndPanel(tokens) +
           ComponentStyleSheets::dialogsMenusAndTooltips(tokens) +
           ComponentStyleSheets::scrollbarsAndStatus(tokens) +
           ComponentStyleSheets::sourceControl(tokens) +
           ComponentStyleSheets::runDebug(tokens);
}

} // namespace

QString Theme::darkStyleSheet() { return composeStyleSheet(tokens(true)); }

QString Theme::lightStyleSheet() { return composeStyleSheet(tokens(false)); }

} // namespace litecode::ui
