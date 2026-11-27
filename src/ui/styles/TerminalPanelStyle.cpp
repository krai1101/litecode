#include "ui/styles/ComponentStyleSheets.h"

#include "ui/styles/StyleTemplate.h"

namespace litecode::ui {

QString ComponentStyleSheets::terminalAndPanel(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QDockWidget#bottomDock { background: @panel; border: 0; }
        QWidget#bottomTabs,
        QWidget#terminalToolbar, QWidget#terminalBody { background: @panel; border: 0; }
        QWidget#bottomPanelContainer {
            background: @panel; border: 0; border-top: 1px solid @border;
        }
        QWidget#bottomTabs, QWidget#bottomTabs QStackedWidget { background: @panel; border: 0; }
        QWidget#bottomTabs QTabBar { background: @panel; border: 0; }
        QWidget#bottomTabs QTabBar::tab {
            background: transparent; border: 0; color: @muted; font-size: 13px;
            min-width: 0; min-height: 26px; padding: 0 9px;
            margin: 4px 2px; border-radius: 4px;
        }
        QWidget#bottomTabs QTabBar::tab:hover { color: @foreground; }
        QWidget#bottomTabs QTabBar::tab:selected {
            background: @selection; color: @listSelectionForeground; border: 0;
        }
        QWidget#panelHeader { background: @panel; border: 0; }
        QWidget#panelHeader QLineEdit, QWidget#panelHeader QComboBox {
            font-size: 12px; padding: 1px 6px; min-height: 20px;
        }
        QPlainTextEdit#extensionOutputText, QPlainTextEdit#debugConsole,
        QPlainTextEdit#gitOutput {
            background: @panel; color: @foreground; border: 0; padding: 6px 10px;
            selection-background-color: @selection;
        }
        QWidget#debugConsolePrompt { background: @panel; border-top: 1px solid @border; }
        QLineEdit#debugConsoleInput {
            background: transparent; color: @foreground; border: 0;
            border-radius: 0; padding: 4px 0; min-height: 20px;
        }
        QToolButton#terminalToolButton {
            background: transparent; border: 0; border-radius: 3px; color: @muted;
            min-width: 25px; min-height: 23px; padding: 0;
        }
        QToolButton#terminalToolButton::menu-indicator { image: none; width: 0; }
        QToolButton#terminalToolButton:hover { background: @hover; color: @foreground; }
        QToolButton#terminalDropdownButton {
            background: transparent; border: 0; color: @muted;
            min-width: 14px; max-width: 14px; min-height: 23px; padding: 0;
        }
        QToolButton#terminalDropdownButton::menu-indicator { image: none; width: 0; }
        QToolButton#terminalDropdownButton:hover { background: transparent; color: @foreground; }
        QAbstractScrollArea#terminalOutput {
            background: @panel; color: @foreground;
        }
        QFrame#terminalCaret { background: @foreground; border: 0; }
        QListWidget#terminalSessionList {
            background: @panel; border: 0; border-left: 1px solid @border;
            color: @foreground; outline: 0; padding: 0;
        }
        QListWidget#terminalSessionList::item {
            background: transparent; min-height: 22px; max-height: 22px; padding: 0;
        }
        QListWidget#terminalSessionList::item:hover { background: transparent; }
        QListWidget#terminalSessionList::item:selected {
            background: @selection; color: @foreground;
        }
        QFrame#terminalFindBar {
            background: @findWidget; border: 1px solid @border;
            border-bottom-left-radius: 4px; border-bottom-right-radius: 4px;
            font-size: 12px;
        }
        QFrame#terminalFindInputFrame {
            background: @input; border: 1px solid @inputBorder; border-radius: 3px;
            min-height: 23px; max-height: 23px;
        }
        QFrame#terminalFindInputFrame[focused="true"] { border-color: @searchFocusBorder; }
        QLineEdit#terminalFindInput {
            background: transparent; color: @foreground; border: 0; padding: 4px 6px;
            font-size: 13px; min-height: 15px; max-height: 15px;
            selection-background-color: @selection; selection-color: @foreground;
        }
        QLabel#terminalFindCount {
            color: @muted; background: transparent; font-size: 12px; padding-left: 5px;
        }
        QToolButton#terminalFindOption {
            background: transparent; color: @muted; border: 1px solid transparent;
            border-radius: 3px; font-size: 12px;
            min-width: 18px; max-width: 18px; min-height: 18px; max-height: 18px;
            padding: 0;
        }
        QToolButton#terminalFindAction {
            background: transparent; color: @muted; border: 0; border-radius: 4px;
            min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px;
            padding: 0;
        }
        QToolButton#terminalFindOption:hover, QToolButton#terminalFindAction:hover {
            background: @hover; color: @foreground;
        }
        QToolButton#terminalFindOption:checked {
            background: @findOptionActiveBackground; border-color: @findOptionActiveBorder;
            color: @findOptionActiveForeground;
        }
    )QSS"),
                            tokens);
}

} // namespace litecode::ui
