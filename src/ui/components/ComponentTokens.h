#pragma once

#include <QColor>
#include <QString>

namespace litecode::ui {
struct ThemeTokens;
}

namespace litecode::ui::components {

struct ComponentMetrics final {
    int controlHeight{30};
    int compactControlHeight{24};
    int listRowHeight{24};
    int radiusSmall{4};
    int radiusMedium{6};
    int spacingSmall{4};
    int spacingMedium{8};
    int horizontalPadding{12};
    int menuWidth{220};
    int contextMenuMinimumWidth{160};
    int bodyFontSize{13};
    int labelFontSize{12};
    int regularWeight{400};
    int strongWeight{600};
};

struct ButtonTokens final {
    QColor foreground;
    QColor background;
    QColor border;
    QColor hoverBackground;
    QColor pressedBackground;
    QColor disabledForeground;
    QColor disabledBackground;
    QColor primaryForeground;
    QColor primaryBackground;
    QColor primaryHoverBackground;
    QColor dangerForeground;
    QColor dangerHoverBackground;
};

struct InputTokens final {
    QColor foreground;
    QColor background;
    QColor border;
    QColor focusBorder;
    QColor selection;
    QColor disabledForeground;
    QColor disabledBackground;
    QColor errorBorder;
};

struct ListTokens final {
    QColor foreground;
    QColor background;
    QColor border;
    QColor hoverBackground;
    QColor selectionBackground;
};

struct OverlayTokens final {
    QColor foreground;
    QColor mutedForeground;
    QColor background;
    QColor border;
    QColor shadow;
};

struct DropdownTokens final {
    QColor foreground;
    QColor background;
    QColor border;
    QColor listBackground;
    QColor listBorder;
};

struct MenuTokens final {
    QColor background;
    QColor border;
    QString arrowIcon;
};

struct ComponentTokens final {
    ComponentMetrics metrics;
    ButtonTokens button;
    InputTokens input;
    ListTokens list;
    OverlayTokens overlay;
    DropdownTokens dropdown;
    MenuTokens menu;
};

[[nodiscard]] ComponentTokens resolveComponentTokens(const ThemeTokens& theme);

} // namespace litecode::ui::components
