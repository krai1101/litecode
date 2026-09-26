#include "ui/styles/StyleTemplate.h"

#include "ui/Theme.h"

#include <QColor>

#include <initializer_list>
#include <utility>

namespace litecode::ui {
QString styleColor(const QColor& color) {
    if (color.alpha() == 255) {
        return color.name();
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

QString applyThemeTokens(QString styleSheet, const ThemeTokens& tokens) {
    const std::initializer_list<std::pair<QString, QString>> replacements{
        {QStringLiteral("@app"), styleColor(tokens.appSurface)},
        {QStringLiteral("@editor"), styleColor(tokens.editorSurface)},
        {QStringLiteral("@sidebar"), styleColor(tokens.sidebarSurface)},
        {QStringLiteral("@panel"), styleColor(tokens.panelSurface)},
        {QStringLiteral("@elevated"), styleColor(tokens.elevatedSurface)},
        {QStringLiteral("@findWidget"), styleColor(tokens.findWidgetSurface)},
        {QStringLiteral("@findOptionActiveBackground"),
         styleColor(tokens.findOptionActiveBackground)},
        {QStringLiteral("@findOptionActiveBorder"), styleColor(tokens.findOptionActiveBorder)},
        {QStringLiteral("@findOptionActiveForeground"),
         styleColor(tokens.findOptionActiveForeground)},
        {QStringLiteral("@settingsSurface"), styleColor(tokens.settingsSurface)},
        {QStringLiteral("@settingsHeaderForeground"), styleColor(tokens.settingsHeaderForeground)},
        {QStringLiteral("@inputBorder"), styleColor(tokens.inputBorder)},
        {QStringLiteral("@input"), styleColor(tokens.inputSurface)},
        {QStringLiteral("@foreground"), styleColor(tokens.foreground)},
        {QStringLiteral("@muted"), styleColor(tokens.mutedForeground)},
        {QStringLiteral("@border"), styleColor(tokens.border)},
        {QStringLiteral("@hover"), styleColor(tokens.hover)},
        {QStringLiteral("@selection"), styleColor(tokens.selection)},
        {QStringLiteral("@pressed"), styleColor(tokens.pressed)},
        {QStringLiteral("@focus"), styleColor(tokens.focus)},
        {QStringLiteral("@searchFocusBorder"), styleColor(tokens.searchFocusBorder)},
        {QStringLiteral("@listHoverBackground"), styleColor(tokens.listHoverBackground)},
        {QStringLiteral("@listSelectionForeground"), styleColor(tokens.listSelectionForeground)},
        {QStringLiteral("@listSelectionBackground"), styleColor(tokens.listSelectionBackground)},
        {QStringLiteral("@accent"), styleColor(tokens.accent)},
        {QStringLiteral("@error"), styleColor(tokens.error)},
        {QStringLiteral("@warning"), styleColor(tokens.warning)},
        {QStringLiteral("@success"), styleColor(tokens.success)},
        {QStringLiteral("@disabledSurface"), styleColor(tokens.disabledSurface)},
        {QStringLiteral("@disabledForeground"), styleColor(tokens.disabledForeground)},
        {QStringLiteral("@onAccent"), styleColor(tokens.onAccent)},
        {QStringLiteral("@closeHover"), styleColor(tokens.closeHover)},
        {QStringLiteral("@titleBarControlHover"), styleColor(tokens.titleBarControlHover)},
        {QStringLiteral("@titleBarCloseHover"), styleColor(tokens.titleBarCloseHover)},
        {QStringLiteral("@scrollbarHover"), styleColor(tokens.scrollbarHover)},
        {QStringLiteral("@scrollbar"), styleColor(tokens.scrollbar)},
        {QStringLiteral("@scrollbarActive"), styleColor(tokens.scrollbarActive)},
    };
    for (const auto& [name, value] : replacements) {
        styleSheet.replace(name, value);
    }
    return styleSheet;
}

} // namespace litecode::ui
