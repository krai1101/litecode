#include "ui/components/ComponentTokens.h"

#include "ui/Theme.h"

namespace litecode::ui::components {

ComponentTokens resolveComponentTokens(const ThemeTokens& theme) {
    ComponentTokens result;
    result.button = {theme.foreground,      theme.secondaryButtonBackground,
                     theme.border,          theme.secondaryButtonHover,
                     theme.pressed,         theme.disabledForeground,
                     theme.disabledSurface, theme.onAccent,
                     theme.accent,          theme.primaryButtonHover,
                     theme.onAccent,        theme.closeHover};
    result.input = {theme.foreground,
                    theme.inputSurface,
                    theme.inputBorder,
                    theme.focus,
                    theme.textSelectionBackground,
                    theme.disabledForeground,
                    theme.disabledSurface,
                    theme.error,
                    theme.warning,
                    theme.accent};
    result.list = {theme.foreground, theme.inputSurface, theme.inputBorder, theme.hover,
                   theme.selection};
    result.overlay = {theme.foreground, theme.mutedForeground, theme.elevatedSurface, theme.border,
                      theme.shadow};
    result.dropdown = {theme.dropdownForeground, theme.dropdownSurface, theme.dropdownBorder,
                       theme.dropdownListSurface, theme.dropdownListBorder};
    result.menu = {theme.menuSurface,
                   theme.menuBorder,
                   theme.menuArrowIcon,
                   theme.menuSelectionBackground,
                   theme.menuSelectionForeground,
                   theme.disabledForeground,
                   theme.foreground};
    // menu.separatorBackground inherits foreground at 20% opacity in VS Code.
    result.menu.separatorBackground.setAlpha(51);
    return result;
}

} // namespace litecode::ui::components
