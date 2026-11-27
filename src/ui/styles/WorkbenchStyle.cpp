#include "ui/styles/ComponentStyleSheets.h"

#include "ui/styles/StyleTemplate.h"

namespace litecode::ui {

QString ComponentStyleSheets::workbench(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QMainWindow#mainWindow, QMainWindow#mainWindow > QWidget {
            background: @app; color: @foreground;
        }
        QMainWindow#mainWindow::separator {
            background: transparent;
        }
        QMainWindow#mainWindow::separator:hover { background: transparent; }
        QTabBar#sidebarTabs { background: transparent; border: 0; }
        QTabBar#sidebarTabs::tab {
            background: transparent; color: @muted; border: 0;
            padding: 5px 8px; margin: 2px; border-radius: 4px;
        }
        QTabBar#sidebarTabs::tab:selected { background: @selection; color: @listSelectionForeground; }
        QTabBar#sidebarTabs::tab:hover { background: @hover; }
        QWidget#workbenchTitleBar { background: transparent; border: 0; }
        QMenuBar {
            background: transparent; color: @foreground; border: 0;
            padding: 6px 0 7px 0; spacing: 0;
            font-size: 13px;
        }
        QMenuBar::item {
            background: transparent; color: @foreground; padding: 3px 8px 4px 8px;
            border-radius: 5px; min-height: 22px; max-height: 22px;
        }
        QMenuBar::item:selected, QMenuBar::item:pressed {
            background: @hover; color: @foreground;
        }
        QMenuBar::item:disabled { background: transparent; color: @muted; }
        QWidget#navigationBar { background: transparent; border: 0; spacing: 2px; padding: 0; }
        QWidget#navigationBar QToolButton {
            background: transparent; border: 0; border-radius: 4px; color: @muted;
            min-width: 26px; min-height: 24px; padding: 0 4px;
        }
        QWidget#navigationBar QToolButton:hover { background: @hover; color: @foreground; }
        QWidget#titleControls { background: transparent; border: 0; spacing: 2px; padding: 0; }
        QWidget#titleControls QToolButton {
            background: transparent; border: 0; border-radius: 4px; color: @foreground; padding: 0;
        }
        QWidget#titleControls QToolButton:hover { background: @hover; }
        QWidget#titleControls QToolButton#windowMinimize,
        QWidget#titleControls QToolButton#windowMaximize,
        QWidget#titleControls QToolButton#windowClose { border-radius: 0; padding: 0; }
        QWidget#titleControls QToolButton#windowMinimize:hover,
        QWidget#titleControls QToolButton#windowMaximize:hover {
            background: @titleBarControlHover;
        }
        QWidget#titleControls QToolButton#windowClose:hover {
            background: @titleBarCloseHover; color: @onAccent;
        }
        QLabel#appMark { background: transparent; border: 0; }
        QLineEdit#commandCenter {
            background: @input; border: 1px solid @border; border-radius: 6px;
            color: @foreground; min-height: 24px; padding: 0 12px;
        }
        QLineEdit#commandCenter:hover { background: @hover; }
        QLineEdit#commandCenter:focus { background: @input; border-color: @focus; }
        QToolBar#activityBar {
            background: @sidebar; border: 0; border-right: 1px solid @border;
            spacing: 0; padding: 0;
        }
        QToolBar#activityBar QToolButton {
            background: transparent; border: 0; min-width: 39px; max-width: 39px;
            min-height: 40px; max-height: 40px; padding: 0; margin: 4px;
        }
        QToolBar#activityBar QToolButton:hover { background: @hover; border-radius: 4px; }
        QToolBar#activityBar QToolButton:checked,
        QToolBar#activityBar QToolButton:checked:hover { background: @selection; border-radius: 4px; }
        QToolBar#activityBar::separator { height: 2px; background: transparent; }
        QDockWidget { background: @sidebar; color: @foreground; border: 0; }
        QDockWidget::title { background: @sidebar; height: 0; padding: 0; }
        QMainWindow#mainWindow > QTabBar {
            background: @sidebar; border: 0; qproperty-drawBase: false;
        }
        QMainWindow#mainWindow > QTabBar::tab {
            background: transparent; color: @foreground; border: 0;
            border-radius: 4px; padding: 4px 10px; margin: 3px 2px;
        }
        QMainWindow#mainWindow > QTabBar::tab:selected { background: @selection; }
        QMainWindow#mainWindow > QTabBar::tab:hover { background: @hover; }
        QWidget#dockHeader { background: @sidebar; border: 0; }
        QLabel#dockHeaderTitle {
            color: @foreground; font-size: 13px; font-weight: 600; padding: 0; min-height: 35px;
        }
        QToolButton#dockHeaderButton {
            background: transparent; border: 0; border-radius: 4px; color: @muted;
            font-size: 13px; min-width: 24px; min-height: 24px; padding: 0;
        }
        QToolButton#dockHeaderButton:hover { background: @hover; color: @foreground; }
        QToolButton#workspaceRootLabel, QToolButton#debugSectionHeader {
            background: transparent; border: 0; border-radius: 0;
            color: @foreground; font-size: 13px; text-align: left;
        }
        QToolButton#workspaceRootLabel:hover, QToolButton#workspaceRootLabel:checked,
        QToolButton#workspaceRootLabel:checked:hover, QToolButton#debugSectionHeader:hover {
            background: transparent;
        }
        QLineEdit[uiRole="inlineInput"] {
            background: @input; color: @foreground; border: 1px solid @focus;
            border-radius: 4px; padding: 1px 2px;
        }
        QWidget#hiddenDockHeader { background: @app; border: 0; }
        QDockWidget#workspaceDock {
            background: @sidebar; border: 0;
        }
        QWidget#workspaceDockBody { background: @sidebar; border: 0; }
        QWidget#workspaceEdgeBorder { background: @border; border: 0; }
        QStackedWidget#workspacePages { background: @sidebar; border: 0; }
    )QSS"),
                            tokens);
}

} // namespace litecode::ui
