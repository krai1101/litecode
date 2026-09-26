#include "ui/components/ComponentTokens.h"

#include "ui/Theme.h"

namespace litecode::ui::components {

ComponentTokens resolveComponentTokens(const ThemeTokens& theme) {
    ComponentTokens result;
    result.button = {theme.foreground,      theme.inputSurface, theme.border,
                     theme.hover,           theme.pressed,      theme.disabledForeground,
                     theme.disabledSurface, theme.onAccent,     theme.accent,
                     theme.focus,           theme.onAccent,     theme.closeHover};
    result.input = {theme.foreground, theme.inputSurface,       theme.inputBorder,     theme.focus,
                    theme.selection,  theme.disabledForeground, theme.disabledSurface, theme.error};
    result.list = {theme.foreground, theme.inputSurface, theme.inputBorder, theme.hover,
                   theme.selection};
    result.overlay = {theme.foreground, theme.mutedForeground, theme.elevatedSurface, theme.border,
                      theme.shadow};
    result.dropdown = {theme.dropdownForeground, theme.dropdownSurface, theme.dropdownBorder,
                       theme.dropdownListSurface, theme.dropdownListBorder};
    result.menu = {theme.menuSurface, theme.menuBorder, theme.menuArrowIcon};
    return result;
}

} // namespace litecode::ui::components
