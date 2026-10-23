#include "ui/styles/ComponentStyleSheets.h"

#include "ui/styles/StyleTemplate.h"

namespace litecode::ui {

QString ComponentStyleSheets::explorer(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QWidget#explorerPage, QWidget#fileTreeHost, QWidget#searchPage,
        QListWidget#workspaceSearchResults, QWidget#workspaceSearchViewport {
            background: @sidebar; border: 0;
        }
        QListWidget#workspaceSearchResults { color: @foreground; }
        QWidget#searchHeaderActions { background: transparent; }
        QToolButton#searchHeaderAction {
            background: transparent; border: 0; border-radius: 3px;
            min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px;
            padding: 0;
        }
        QToolButton#searchHeaderAction:hover { background: @hover; }
        QWidget#workspaceSearchRow, QWidget#workspaceReplaceRow { background: transparent; }
        QWidget#workspaceSearchBox {
            background: @input; border: 1px solid @inputBorder; border-radius: 3px;
            min-height: 28px; max-height: 28px;
        }
        QWidget#workspaceSearchBox:focus-within { border-color: @searchFocusBorder; }
        QLineEdit#workspaceSearchInput, QLineEdit#workspaceReplaceInput {
            background: transparent; border: 0; border-radius: 0;
            color: @foreground; padding: 0 6px;
            selection-background-color: @selection;
        }
        QLineEdit#workspaceSearchInput:disabled, QLineEdit#workspaceReplaceInput:disabled {
            background: @disabledSurface; color: @disabledForeground;
        }
        QToolButton#searchReplaceToggle {
            background: transparent; border: 0; padding: 0;
            min-width: 18px; max-width: 18px; min-height: 28px; max-height: 28px;
        }
        QToolButton#searchReplaceToggle:hover { background: @hover; }
        QToolButton#workspaceSearchOption {
            background: transparent; border: 0; border-radius: 2px; color: @foreground;
            font-size: 12px; padding: 0 2px;
        }
        QToolButton#workspaceSearchOption:hover { background: @hover; }
        QToolButton#workspaceSearchOption:checked {
            background: @selection; color: @foreground;
        }
        QToolButton#workspaceReplaceAllButton {
            background: transparent; border: 0; border-radius: 3px; padding: 0;
            min-width: 28px; max-width: 28px;
        }
        QToolButton#workspaceReplaceAllButton:hover {
            background: @hover;
        }
        QListWidget#workspaceSearchResults::item {
            min-height: 22px; padding: 0; border: 0;
        }
        QListWidget#workspaceSearchResults::item:hover { background: @hover; }
        QListWidget#workspaceSearchResults::item:selected {
            background: @selection; color: @foreground;
        }
        QTreeView#fileTree {
            background: @sidebar; color: @foreground; border: 0;
            show-decoration-selected: 1; padding-left: 10px;
            font-size: 13px;
            selection-background-color: @selection; selection-color: @foreground;
        }
        QTreeView#fileTree QScrollBar { background: transparent; }
        QTreeView#fileTree QWidget#qt_scrollarea_vcontainer,
        QTreeView#fileTree QWidget#qt_scrollarea_hcontainer { background: @sidebar; border: 0; }
        QTreeView#fileTree::branch, QTreeView#fileTree::item,
        QTreeView#fileTree::branch:hover, QTreeView#fileTree::item:hover,
        QTreeView#fileTree::branch:selected, QTreeView#fileTree::item:selected {
            background: transparent; border: 0;
        }
        QTreeView#fileTree::item { min-height: 24px; }
        QLineEdit#explorerInlineEditor {
            background: @input; color: @foreground; border: 1px solid @focus;
            border-radius: 4px; padding: 1px 2px;
        }
        QWidget#workspaceRootBar { background: @sidebar; min-height: 23px; }
        QWidget#workspaceRootBar[selected="true"] { background: @selection; }
        QToolButton#workspaceRootLabel {
            background: transparent; border: 0; border-radius: 0; color: @foreground;
            font-size: 13px; font-weight: 600; min-height: 23px; padding: 0; text-align: left;
        }
        QToolButton#workspaceRootChevron { background: transparent; border: 0; padding: 0; }
        QToolButton#workspaceRootLabel:hover,
        QToolButton#workspaceRootLabel:checked,
        QToolButton#workspaceRootLabel:checked:hover { background: transparent; }
        QToolButton#explorerActionButton {
            background: transparent; border: 0; border-radius: 3px; color: @muted;
            min-width: 22px; max-width: 22px; min-height: 23px; padding: 0;
        }
        QToolButton#explorerActionButton:hover { background: @hover; color: @foreground; }
    )QSS"),
                            tokens);
}

} // namespace litecode::ui
