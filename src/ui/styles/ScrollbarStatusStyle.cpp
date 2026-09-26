#include "ui/styles/ComponentStyleSheets.h"

#include "ui/styles/StyleTemplate.h"

namespace litecode::ui {

QString ComponentStyleSheets::scrollbarsAndStatus(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QStatusBar#statusBar {
            background: @panel; color: @muted; border: 0; border-top: 1px solid @border;
        }
        QStatusBar#statusBar QLabel { background: transparent; color: @muted; font-size: 12px; }
        QToolButton#indentStatusButton, QToolButton#encodingStatusButton {
            background: transparent; border: 0; color: @muted; font-size: 12px;
            padding: 0 4px;
        }
        QToolButton#indentStatusButton::menu-indicator,
        QToolButton#encodingStatusButton::menu-indicator { image: none; width: 0; }
        QToolButton#indentStatusButton:hover,
        QToolButton#encodingStatusButton:hover { background: @hover; color: @foreground; }
        QStatusBar#statusBar::item { border: 0; }
        QScrollBar:vertical { background: transparent; border: 0; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: transparent; border: 0; min-height: 28px; }
        QScrollBar[scrollbarActive="true"]::handle:vertical { background: @scrollbar; }
        QScrollBar[scrollbarActive="true"]::handle:vertical:hover { background: @scrollbarHover; }
        QScrollBar[scrollbarActive="true"]::handle:vertical:pressed { background: @scrollbarActive; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            background: transparent; border: 0; height: 0; width: 0;
        }
        QScrollBar:horizontal { background: transparent; border: 0; height: 10px; margin: 0; }
        QScrollBar::handle:horizontal { background: transparent; border: 0; min-width: 28px; }
        QScrollBar[scrollbarActive="true"]::handle:horizontal { background: @scrollbar; }
        QScrollBar[scrollbarActive="true"]::handle:horizontal:hover { background: @scrollbarHover; }
        QScrollBar[scrollbarActive="true"]::handle:horizontal:pressed { background: @scrollbarActive; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            background: transparent; border: 0; height: 0; width: 0;
        }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; border: 0; }
    )QSS"),
                            tokens);
}

} // namespace litecode::ui
