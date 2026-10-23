#include "ui/styles/ComponentStyleSheets.h"

#include "ui/styles/StyleTemplate.h"

namespace litecode::ui {

QString ComponentStyleSheets::editorChrome(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QWidget#editorArea { background: @editor; }
        QTabWidget#editorTabs::pane { background: @editor; border: 0; }
        QTabBar#editorTabBar {
            background: @sidebar; border: 0; border-bottom: 1px solid @border;
        }
        QTabBar#editorTabBar::scroller { width: 0px; }
        QScrollBar#editorTabOverflowScroll {
            background: @sidebar; border: 0; height: 4px; margin: 0;
        }
        QScrollBar#editorTabOverflowScroll::handle:horizontal {
            background: @scrollbar; border: 0; min-width: 24px;
        }
        QScrollBar#editorTabOverflowScroll::handle:horizontal:hover {
            background: @scrollbarHover;
        }
        QScrollBar#editorTabOverflowScroll::add-line:horizontal,
        QScrollBar#editorTabOverflowScroll::sub-line:horizontal {
            width: 0; background: transparent; border: 0;
        }
        QTabBar#editorTabBar::tab {
            background: @sidebar; border: 0; border-right: 1px solid @border;
            color: @muted; min-width: 86px; min-height: 33px; padding: 0 8px;
        }
        QTabBar#editorTabBar::tab:hover { background: @hover; color: @foreground; }
        QTabBar#editorTabBar::tab:selected {
            background: @editor; color: @foreground; border-top: 1px solid @accent;
        }
        QToolButton#editorTabClose {
            background: transparent; border: 0; border-radius: 3px; padding: 2px;
        }
        QToolButton#editorTabClose:hover { background: @hover; }
        QToolButton#editorTabClose:pressed { background: @pressed; }
        QWidget#editorTabActions { background: transparent; border: 0; }
        QLabel#editorTabDirtyIndicator {
            background: transparent; border: 0; color: @muted; font-size: 16px;
            padding: 0;
        }
        QWidget#editorBreadcrumb {
            background: @editor; color: @muted; border-bottom: 1px solid @border;
            font-size: 12px; min-height: 25px;
        }
        QLabel#breadcrumbSegment, QLabel#deletedBreadcrumbSegment, QLabel#breadcrumbSeparator {
            background: transparent; border: 0; color: @muted;
        }
        QFrame#findReplaceBar {
            background: @findWidget; border: 1px solid @border; border-radius: 5px;
            font-size: 12px;
        }
        QFrame#findInputFrame {
            background: @input; border: 1px solid @inputBorder; border-radius: 3px;
        }
        QFrame#findInputFrame[focused="true"] { border-color: @searchFocusBorder; }
        QFrame#findReplaceBar QPlainTextEdit {
            background: @input; border: 1px solid @inputBorder; border-radius: 3px;
            color: @foreground; font-size: 13px; padding: 2px 6px;
        }
        QFrame#findReplaceBar QPlainTextEdit#findInput {
            background: transparent; border: 0; padding: 2px 6px;
        }
        QFrame#findReplaceBar QPlainTextEdit#replaceInput:focus { border-color: @searchFocusBorder; }
        QFrame#findReplaceBar QPlainTextEdit:disabled {
            background: @disabledSurface; color: @disabledForeground;
        }
        QLabel#findResultLabel {
            color: @muted; background: transparent; font-size: 12px; padding-left: 2px;
        }
        QToolButton#findToggleButton {
            background: transparent; border: 0; border-radius: 3px; color: @muted;
            min-width: 18px; max-width: 18px; min-height: 22px; max-height: 22px;
            padding: 0;
        }
        QToolButton#findOptionButton {
            background: transparent; border: 1px solid transparent; border-radius: 3px;
            color: @muted; font-size: 12px;
            min-width: 18px; max-width: 18px; min-height: 18px; max-height: 18px;
            padding: 0;
        }
        QToolButton#findActionButton, QToolButton#findSelectionButton {
            background: transparent; border: 0; border-radius: 5px; color: @muted;
            min-width: 22px; max-width: 22px; min-height: 22px; max-height: 22px;
            padding: 0;
        }
        QToolButton#findToggleButton:hover, QToolButton#findOptionButton:hover,
        QToolButton#findActionButton:hover, QToolButton#findSelectionButton:hover {
            background: @hover; color: @foreground;
        }
        QToolButton#findOptionButton:checked {
            background: @findOptionActiveBackground; border-color: @findOptionActiveBorder;
            color: @findOptionActiveForeground;
        }
        QToolButton#findSelectionButton:checked {
            background: @findOptionActiveBackground; color: @findOptionActiveForeground;
        }
        QLabel#quickOpenResult { background: transparent; color: @foreground; padding: 3px 7px; }
        QLabel#quickOpenResult b { color: @foreground; }
        QLabel#welcomeBrand { color: @muted; font-size: 30px; font-weight: 600; }
        QLabel#welcomeTagline { color: @muted; font-size: 14px; margin-bottom: 18px; }
        QWidget#welcomeShortcuts { background: transparent; }
        QLabel#welcomeAction { color: @foreground; font-size: 13px; }
        QLabel#welcomeKeybinding { color: @muted; font-size: 12px; }
    )QSS"),
                            tokens);
}

} // namespace litecode::ui
