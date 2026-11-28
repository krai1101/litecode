#include "ui/components/ComponentStyle.h"

#include "ui/components/ComponentTokens.h"
#include "ui/styles/StyleTemplate.h"

#include <initializer_list>
#include <utility>

namespace litecode::ui::components {

QString componentStyleSheet(const ComponentTokens& tokens) {
    QString style = QStringLiteral(R"QSS(
        QDialog { background: @overlayBackground; color: @overlayForeground; }
        QDialog[uiComponent="dialog"] {
            background: @overlayBackground; color: @overlayForeground;
            border: 1px solid @overlayBorder; border-radius: @surfaceRadiusPx;
        }
        QWidget[uiComponent="dialogHeader"] {
            background: @overlayBackground; color: @overlayForeground; border: 0;
        }
        QLabel#dialogTitle {
            background: transparent; color: @overlayForeground;
            font-size: @bodyFontPx; font-weight: @strongWeight;
        }
        QFrame#confirmationSeparator { background: @overlayBorder; border: 0; }

        *[uiComponent="button"] {
            min-height: @controlHeightPx; padding: 3px @horizontalPaddingPx;
            border: 1px solid @buttonBorder; border-radius: @controlRadiusPx;
            background: @buttonBackground; color: @buttonForeground;
        }
        *[uiComponent="button"]:hover { background: @buttonHover; }
        *[uiComponent="button"]:pressed { background: @buttonPressed; }
        *[uiComponent="button"]:disabled {
            background: @buttonDisabledBackground; color: @buttonDisabledForeground;
        }
        *[uiComponent="button"][uiDensity="compact"] {
            min-height: 18px; padding: 2px @spacingMediumPx;
        }
        *[uiComponent="button"][uiVariant="ghost"] {
            background: transparent; border-color: transparent;
        }
        *[uiComponent="button"][uiVariant="ghost"]:hover { background: @buttonHover; }
        *[uiComponent="button"][uiVariant="ghost"]:pressed { background: @buttonPressed; }
        *[uiComponent="button"][uiVariant="ghost"]:disabled { background: transparent; }
        *[uiComponent="button"][uiVariant="primary"] {
            background: @buttonPrimaryBackground; color: @buttonPrimaryForeground;
            border-color: @buttonPrimaryBackground;
        }
        *[uiComponent="button"][uiVariant="primary"]:hover {
            background: @buttonPrimaryHover; border-color: @buttonPrimaryHover;
        }
        *[uiComponent="button"][uiVariant="primary"]:disabled {
            background: @buttonDisabledBackground; border-color: @buttonDisabledBackground;
            color: @buttonDisabledForeground;
        }
        *[uiComponent="button"][uiVariant="danger"]:hover {
            background: @buttonDangerHover; color: @buttonDangerForeground;
            border-color: @buttonDangerHover;
        }
        *[uiComponent="iconButton"] {
            background: transparent; color: @overlayMuted; border: 0;
            border-radius: @controlRadiusPx; padding: 0;
        }
        *[uiComponent="iconButton"]:hover {
            background: @buttonHover; color: @buttonForeground;
        }
        *[uiComponent="iconButton"]:pressed { background: @buttonPressed; }
        *[uiComponent="iconButton"][uiVariant="danger"]:hover {
            background: @buttonDangerHover; color: @buttonDangerForeground;
        }
        QToolButton#dialogClose { font-size: 18px; }

        *[uiComponent="input"] {
            background: @inputBackground; color: @inputForeground;
            border: 1px solid @inputBorder; border-radius: @controlRadiusPx;
            min-height: @compactHeightPx; padding: 2px @spacingMediumPx;
            selection-background-color: @inputSelection;
            selection-color: @inputForeground;
        }
        *[uiComponent="input"]:focus { border-color: @inputFocusBorder; }
        *[uiComponent="input"]:disabled {
            background: @inputDisabledBackground; color: @inputDisabledForeground;
        }
        *[uiComponent="input"][uiState="error"] { border-color: @inputErrorBorder; }
        *[uiComponent="input"][uiState="warning"] { border-color: @inputWarningBorder; }
        *[uiComponent="input"][uiState="info"] { border-color: @inputInfoBorder; }
        QLabel#quickInputValidation[validationSeverity="3"] { color: @inputErrorBorder; }
        QLabel#quickInputValidation[validationSeverity="2"] { color: @inputWarningBorder; }
        QLabel#quickInputValidation[validationSeverity="1"] { color: @inputInfoBorder; }
        *[uiComponent="pickerLabel"] { background: transparent; color: @inputForeground; border: 0; padding: 0; }
        *[uiComponent="pickerDetail"] { background: transparent; color: @overlayMuted; border: 0; padding: 0; }
        *[uiComponent="statusBarItem"] { border: 0; border-radius: 0; padding: 0 4px; font-size: 12px; }

        QKeySequenceEdit[uiComponent="input"] QLineEdit {
            min-height: 0; padding: 0; border: 0; border-radius: 0;
            background: @inputBackground; color: @inputForeground;
            selection-background-color: @inputSelection;
            selection-color: @inputForeground;
        }
        QKeySequenceEdit[uiComponent="input"] QToolButton {
            background: transparent; color: @overlayMuted; border: 0; padding: 0;
        }
        QKeySequenceEdit[uiComponent="input"] QToolButton:hover {
            background: @buttonHover; color: @buttonForeground;
        }

        QLabel[uiComponent="choiceTitle"] { color: @inputFocusBorder; font-weight: 600; }
        QLabel[uiComponent="choiceDescription"] { color: @overlayMuted; font-size: 12px; }
        QSlider[uiComponent="choiceSlider"]::groove:horizontal {
            height: 8px; background: @listHover; border-radius: 4px;
        }
        QSlider[uiComponent="choiceSlider"]::sub-page:horizontal {
            background: @inputFocusBorder; border-radius: 4px;
        }
        QSlider[uiComponent="choiceSlider"]::handle:horizontal {
            background: @inputBackground; border: 1px solid @inputBorder;
            width: 20px; margin: -7px 0; border-radius: 10px;
        }
        QComboBox[modernComboBox="true"] {
            background: @dropdownBackground; color: @dropdownForeground;
            border: 1px solid @dropdownBorder;
            min-height: 18px; padding: 2px 23px 2px 8px;
            border-radius: 4px; font-size: 13px;
        }
        QComboBox[modernComboBox="true"]::drop-down {
            subcontrol-origin: padding; subcontrol-position: top right;
            width: 23px; border: 0; background: transparent;
        }
        QComboBox[modernComboBox="true"]::down-arrow {
            image: none; width: 0; height: 0;
        }
        QComboBox[modernComboBox="true"] > QLineEdit {
            min-height: 0; padding: 0; border: 0; border-radius: 0;
            background: transparent; color: @inputForeground;
        }
        QWidget[uiComponent="comboPopup"] QAbstractItemView[uiComponent="list"] {
            background: transparent; color: @dropdownForeground;
            border: 0; border-radius: 0;
        }
        QWidget[uiComponent="comboPopup"] QAbstractItemView[uiComponent="list"]::item {
            height: 22px; min-height: 22px; padding: 0 8px; border: 0; border-radius: 3px;
        }
        QWidget[uiComponent="comboPopup"] QAbstractItemView[uiComponent="list"]::item:selected {
            background: @listHover; color: @dropdownForeground;
        }

        *[uiComponent="list"] {
            background: @listBackground; color: @listForeground;
            border: 1px solid @listBorder; border-radius: @controlRadiusPx; outline: 0;
            selection-background-color: @listSelection; selection-color: @listForeground;
        }
        *[uiComponent="list"]::item {
            border: 0; min-height: @listRowHeightPx; padding: 1px @spacingMediumPx;
        }
        *[uiComponent="list"]::item:hover { background: @listHover; }
        *[uiComponent="list"]::item:selected {
            background: @listSelection; color: @listForeground;
        }
        *[uiComponent="checkbox"]::indicator {
            width: 12px; height: 12px; border: 1px solid @inputBorder;
            border-right: 1px solid @inputBorder;
            border-radius: 2px; background: @inputBackground;
        }
        *[uiComponent="checkbox"]::indicator:checked {
            image: url(:/icons/check.svg);
            background: @buttonPrimaryBackground; border-color: @buttonPrimaryBackground;
        }
        *[uiComponent="group"] {
            background: transparent; color: @overlayForeground;
            border: 1px solid @overlayBorder; border-radius: @controlRadiusPx;
            margin-top: @spacingMediumPx; padding: 10px @spacingMediumPx @spacingMediumPx;
        }
        *[uiComponent="group"]::title {
            subcontrol-origin: margin; subcontrol-position: top left;
            left: 7px; padding: 0 @spacingSmallPx; background: @overlayBackground;
        }
        *[uiComponent="dialogActions"] { background: transparent; }
        *[uiComponent="actionBar"] { background: transparent; border: 0; }

        QMenu {
            background: @menuBackground; color: @overlayForeground;
            border: 1px solid @menuBorder; border-radius: @surfaceRadiusPx; padding: 4px;
        }
        /* Workbench direct-child surfaces must not recolor top-level popup menus. */
        QMainWindow#mainWindow > QMenu[uiComponent="menu"] {
            background: @menuBackground; color: @overlayForeground;
        }
        QMenu::item, QWidget[uiComponent="comboPopup"] QAbstractItemView[menuRows="true"]::item {
            height: @listRowHeightPx; min-height: @listRowHeightPx;
            padding: 0 30px 0 @horizontalPaddingPx; border-radius: 3px;
        }
        QMenu[uiMenuStyle="popover"] { border-radius: 14px; padding: 4px 12px; }
        QMenu::item:selected,
        QWidget[uiComponent="comboPopup"] QAbstractItemView[menuRows="true"]::item:selected,
        QPushButton[uiComponent="menuAction"]:hover,
        QPushButton[uiComponent="menuAction"]:focus {
            background: @menuSelectionBackground; color: @menuSelectionForeground;
        }
        QPushButton[uiComponent="menuAction"] {
            background: transparent; color: @overlayForeground; border: 0; border-radius: 3px;
            min-height: @listRowHeightPx; padding: 0 @horizontalPaddingPx; text-align: left;
        }
        QFrame[uiComponent="menuSeparator"] { background: @menuSeparatorBackground; border: 0; }
        QMenu::item:disabled { color: @menuDisabledForeground; }
        QMenu::indicator { width: 16px; height: 16px; }
        QMenu#themeMenu::item:checked { background: @listSelection; }
        QMenu#themeMenu::indicator { width: 0px; height: 0px; image: none; }
        QMenu::right-arrow {
            image: url(@menuArrowIcon); width: 16px; height: 16px;
            position: relative; right: 8px;
        }
        QMenu::separator {
            height: 1px; background: @menuSeparatorBackground; margin: @spacingSmallPx 0;
        }
        QToolTip {
            background: @overlayBackground; color: @overlayForeground;
            border: 1px solid @overlayBorder; border-radius: @controlRadiusPx;
            padding: 1px @spacingSmallPx; margin: 0;
            min-height: 0; font-size: @labelFontPx; font-weight: @regularWeight;
        }
        QDialog[uiComponent="popupHost"] { background: transparent; }
        QFrame[uiComponent="popup"] {
            background: @overlayBackground; border: 1px solid @overlayBorder;
            border-radius: @surfaceRadiusPx;
        }
    )QSS");

    const auto px = [](int value) { return QStringLiteral("%1px").arg(value); };
    const std::initializer_list<std::pair<QString, QString>> replacements{
        {QStringLiteral("@overlayBackground"), styleColor(tokens.overlay.background)},
        {QStringLiteral("@overlayForeground"), styleColor(tokens.overlay.foreground)},
        {QStringLiteral("@overlayMuted"), styleColor(tokens.overlay.mutedForeground)},
        {QStringLiteral("@overlayBorder"), styleColor(tokens.overlay.border)},
        {QStringLiteral("@buttonForeground"), styleColor(tokens.button.foreground)},
        {QStringLiteral("@buttonBackground"), styleColor(tokens.button.background)},
        {QStringLiteral("@buttonBorder"), styleColor(tokens.button.border)},
        {QStringLiteral("@buttonHover"), styleColor(tokens.button.hoverBackground)},
        {QStringLiteral("@buttonPressed"), styleColor(tokens.button.pressedBackground)},
        {QStringLiteral("@buttonDisabledForeground"), styleColor(tokens.button.disabledForeground)},
        {QStringLiteral("@buttonDisabledBackground"), styleColor(tokens.button.disabledBackground)},
        {QStringLiteral("@buttonPrimaryForeground"), styleColor(tokens.button.primaryForeground)},
        {QStringLiteral("@buttonPrimaryBackground"), styleColor(tokens.button.primaryBackground)},
        {QStringLiteral("@buttonPrimaryHover"), styleColor(tokens.button.primaryHoverBackground)},
        {QStringLiteral("@buttonDangerForeground"), styleColor(tokens.button.dangerForeground)},
        {QStringLiteral("@buttonDangerHover"), styleColor(tokens.button.dangerHoverBackground)},
        {QStringLiteral("@inputForeground"), styleColor(tokens.input.foreground)},
        {QStringLiteral("@inputBackground"), styleColor(tokens.input.background)},
        {QStringLiteral("@inputBorder"), styleColor(tokens.input.border)},
        {QStringLiteral("@inputFocusBorder"), styleColor(tokens.input.focusBorder)},
        {QStringLiteral("@inputSelection"), styleColor(tokens.input.selection)},
        {QStringLiteral("@inputDisabledForeground"), styleColor(tokens.input.disabledForeground)},
        {QStringLiteral("@inputDisabledBackground"), styleColor(tokens.input.disabledBackground)},
        {QStringLiteral("@inputErrorBorder"), styleColor(tokens.input.errorBorder)},
        {QStringLiteral("@inputWarningBorder"), styleColor(tokens.input.warningBorder)},
        {QStringLiteral("@inputInfoBorder"), styleColor(tokens.input.infoBorder)},
        {QStringLiteral("@listForeground"), styleColor(tokens.list.foreground)},
        {QStringLiteral("@listBackground"), styleColor(tokens.list.background)},
        {QStringLiteral("@listBorder"), styleColor(tokens.list.border)},
        {QStringLiteral("@listHover"), styleColor(tokens.list.hoverBackground)},
        {QStringLiteral("@listSelection"), styleColor(tokens.list.selectionBackground)},
        {QStringLiteral("@dropdownForeground"), styleColor(tokens.dropdown.foreground)},
        {QStringLiteral("@dropdownBackground"), styleColor(tokens.dropdown.background)},
        {QStringLiteral("@dropdownBorder"), styleColor(tokens.dropdown.border)},
        {QStringLiteral("@dropdownListBackground"), styleColor(tokens.dropdown.listBackground)},
        {QStringLiteral("@menuBorder"), styleColor(tokens.menu.border)},
        {QStringLiteral("@menuSelectionBackground"), styleColor(tokens.menu.selectionBackground)},
        {QStringLiteral("@menuSelectionForeground"), styleColor(tokens.menu.selectionForeground)},
        {QStringLiteral("@menuDisabledForeground"), styleColor(tokens.menu.disabledForeground)},
        {QStringLiteral("@menuSeparatorBackground"), styleColor(tokens.menu.separatorBackground)},
        {QStringLiteral("@menuBackground"), styleColor(tokens.menu.background)},
        {QStringLiteral("@menuArrowIcon"), tokens.menu.arrowIcon},
        {QStringLiteral("@controlHeightPx"), px(tokens.metrics.controlHeight)},
        {QStringLiteral("@compactHeightPx"), px(tokens.metrics.compactControlHeight)},
        {QStringLiteral("@listRowHeightPx"), px(tokens.metrics.listRowHeight)},
        {QStringLiteral("@controlRadiusPx"), px(tokens.metrics.radiusSmall)},
        {QStringLiteral("@surfaceRadiusPx"), px(tokens.metrics.radiusMedium)},
        {QStringLiteral("@spacingSmallPx"), px(tokens.metrics.spacingSmall)},
        {QStringLiteral("@spacingMediumPx"), px(tokens.metrics.spacingMedium)},
        {QStringLiteral("@horizontalPaddingPx"), px(tokens.metrics.horizontalPadding)},
        {QStringLiteral("@bodyFontPx"), px(tokens.metrics.bodyFontSize)},
        {QStringLiteral("@labelFontPx"), px(tokens.metrics.labelFontSize)},
        {QStringLiteral("@regularWeight"), QString::number(tokens.metrics.regularWeight)},
        {QStringLiteral("@strongWeight"), QString::number(tokens.metrics.strongWeight)},
    };
    for (const auto& [name, value] : replacements)
        style.replace(name, value);
    return style;
}

} // namespace litecode::ui::components
