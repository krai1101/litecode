#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace litecode::ui {

struct ThemeMetrics final {
    static constexpr int activityBarWidth = 48;
    static constexpr int navigationBarHeight = 35;
    static constexpr int editorTabHeight = 35;
    static constexpr int treeRowHeight = 24;
    static constexpr int statusBarHeight = 22;
    static constexpr int primarySidebarWidth = 270;
    static constexpr int bottomPanelHeight = 210;
};

struct WorkbenchPalette final {
    QColor titleSurface;
    QColor titleSeparator;
};

struct ThemeTokens final {
    QColor appSurface;
    QColor editorSurface;
    QColor sidebarSurface;
    QColor panelSurface;
    QColor elevatedSurface;
    QColor foreground;
    QColor mutedForeground;
    QColor border;
    QColor shadow;
    QColor hover;
    QColor selection;
    QColor focus;
    QColor accent;
    QColor error;
    QColor warning;
    QColor success;
    QColor scrollbar;
    QColor scrollbarHover;
    QColor scrollbarActive;
    QColor inputSurface;
    QColor inputBorder;
    QColor pressed;
    QColor disabledSurface;
    QColor disabledForeground;
    QColor onAccent;
    QColor closeHover;
    QColor settingsSurface;
    QColor settingsHeaderForeground;
    QColor dropdownSurface;
    QColor dropdownBorder;
    QColor dropdownListSurface;
    QColor dropdownListBorder;
    QColor dropdownForeground;
    QColor findWidgetSurface;
    QColor findOptionActiveBackground;
    QColor findOptionActiveBorder;
    QColor findOptionActiveForeground;
    QColor menuSurface;
    QColor menuBorder;
    QString menuArrowIcon;
    QColor titleBarForeground;
    QColor titleBarControlHover;
    QColor titleBarCloseHover;
    QColor searchFocusBorder;
    QColor listHoverBackground;
    QColor listSelectionForeground;
    QColor listSelectionBackground;
    QColor iconForeground;
    QColor terminalSelectionBackground;
    QColor terminalSearchBackground;
    QColor terminalCurrentSearchBackground;
    QColor terminalSessionActionHover;
    QColor windowOutline;
    QColor popupShadow;
};

class Theme final {
  public:
    static void apply(QApplication& application, bool dark);
    [[nodiscard]] static WorkbenchPalette workbenchPalette(bool dark);
    [[nodiscard]] static ThemeTokens tokens(bool dark);
    [[nodiscard]] static QString darkStyleSheet();
    [[nodiscard]] static QString lightStyleSheet();
};

} // namespace litecode::ui
