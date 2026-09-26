#include "ui/styles/ComponentStyleSheets.h"

#include "ui/styles/StyleTemplate.h"

namespace litecode::ui {

QString ComponentStyleSheets::dialogsMenusAndTooltips(const ThemeTokens& tokens) {
    // Feature overlays define layout-specific details only. Shared controls,
    // surfaces, states and variants live in ui/components/ComponentStyle.cpp.
    return applyThemeTokens(QStringLiteral(R"QSS(
        QWidget#confirmationBody { background: @elevated; color: @foreground; }
        QLabel#confirmationMessage {
            background: transparent; color: @foreground; font-size: 13px;
        }

        QLineEdit#commandQuery, QLineEdit#quickOpenQuery { padding: 6px 9px; }
        QLineEdit#commandQuery:focus, QLineEdit#quickOpenQuery:focus,
        QLineEdit#keyboardShortcutsSearch:focus {
            border-color: @searchFocusBorder;
        }
        QLineEdit#commandQuery { font-size: 13px; }
        QListWidget#commandResults, QListWidget#quickOpenResults {
            background: @elevated; border: 0; border-radius: 0;
        }
        QListWidget#commandResults { font-size: 13px; }
        QListWidget#quickOpenResults::item { min-height: 24px; }

        QTableWidget#keyboardShortcutsTable {
            background: @elevated; color: @foreground; border: 1px solid @border;
            outline: 0;
        }
        QDialog#keyboardShortcutsDialog { border: 1px solid @inputBorder; border-radius: 0; }
        QDialog#keyboardShortcutsDialog > QWidget {
            background: @elevated; color: @foreground;
        }
        QTableWidget#keyboardShortcutsTable QHeaderView::section {
            background: @input; color: @foreground; border: 0;
            border-bottom: 1px solid @border; padding: 7px;
        }
        QTableWidget#keyboardShortcutsTable::item {
            border: 0; padding: 4px 8px;
        }
        QTableWidget#keyboardShortcutsTable::item:hover { background: @listHoverBackground; }
        QTableWidget#keyboardShortcutsTable::item:selected {
            background: @listSelectionBackground; color: @listSelectionForeground;
        }
        QLabel#keyboardShortcutsMessage { color: @muted; background: transparent; }
        QLabel#keyboardShortcutsMessage[uiState="error"] { color: @error; }

        QDialog#editorFontDialog QLabel,
        QDialog#editorFontDialog QCheckBox,
        QDialog#editorFontDialog QGroupBox {
            background: transparent; color: @foreground; font-size: 12px;
        }
        QDialog#settingsDialog, QWidget#settingsSearchRegion, QWidget#settingsBody,
        QStackedWidget#settingsPages, QWidget#settingsContent, QWidget#settingsEmptyPage,
        QWidget#settingRow {
            background: @settingsSurface; color: @foreground;
        }
        QDialog#settingsDialog { border: 1px solid @inputBorder; border-radius: 0; }
        QDialog#settingsDialog QWidget#dialogHeader {
            background: @settingsSurface;
        }
        QDialog#settingsDialog QLabel#dialogTitle {
            color: @settingsHeaderForeground;
        }
        QDialog#settingsDialog QLineEdit#settingsSearch {
            min-height: 26px; padding: 3px 8px;
        }
        QDialog#settingsDialog QLineEdit#settingsSearch:focus {
            border-color: @searchFocusBorder;
        }
        QListWidget#settingsNavigation {
            background: @settingsSurface; color: @muted;
            border: 0; border-radius: 0;
            outline: 0; padding: 2px 6px 0 0;
        }
        QListWidget#settingsNavigation::item {
            min-height: 28px; padding: 0 10px; border: 0;
        }
        QListWidget#settingsNavigation::item:hover { background: @hover; }
        QListWidget#settingsNavigation::item:selected {
            background: @listSelectionBackground; color: @foreground; font-weight: 600;
        }
        QFrame#settingsNavigationSeparator { color: @border; }
        QScrollArea#settingsScrollArea,
        QScrollArea#settingsScrollArea QWidget#qt_scrollarea_viewport {
            background: @settingsSurface; border: 0;
        }
        QDialog#settingsDialog QLabel[uiRole="categoryTitle"] {
            color: @settingsHeaderForeground; font-size: 24px; font-weight: 600;
            margin-bottom: 12px;
        }
        QDialog#settingsDialog QLabel[uiRole="sectionTitle"] {
            color: @settingsHeaderForeground; font-size: 18px; font-weight: 600;
            margin-bottom: 8px;
        }
        QDialog#settingsDialog QLabel[uiRole="settingTitle"] {
            color: @foreground; font-size: 13px; font-weight: 600;
        }
        QDialog#settingsDialog QLabel[uiRole="description"] {
            color: @muted; font-size: 12px;
        }
        QDialog#settingsDialog QLabel[uiRole="emptyState"] {
            color: @muted; font-size: 13px; padding: 32px;
        }
        QDialog#settingsDialog QLabel[uiRole="workspaceError"] {
            color: @error; padding: 0 30px 12px 30px;
        }
        QDialog#settingsDialog QWidget#settingRow:hover { background: @hover; }
        QDialog#settingsDialog QComboBox#editorFontFamily { min-width: 430px; }
        QDialog#settingsDialog QComboBox#editorFontWeight { min-width: 180px; }
        QDialog#settingsDialog QSpinBox#editorFontSize { min-width: 110px; }
        QDialog#settingsDialog QComboBox#fileEncoding { min-height: 24px; }
    )QSS"),
                            tokens);
}

} // namespace litecode::ui
