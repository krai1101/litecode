#include "ui/styles/ComponentStyleSheets.h"
#include "ui/styles/StyleTemplate.h"
namespace litecode::ui {
QString ComponentStyleSheets::sourceControl(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QWidget#sourceControlPanel, QWidget#extensionsPanel {
            background: @sidebar; color: @foreground; font-size: 13px;
        }
        QWidget#sourceControlPanel QLabel, QWidget#extensionsPanel QLabel {
            background: transparent; color: @foreground; border: 0;
        }
        QLabel#scmRepository { font-weight: 600; }
        QWidget#sourceControlPanel QPushButton#gitCommitButton {
            min-height: 28px; max-height: 28px; padding: 0 8px;
        }
        QWidget#sourceControlPanel QPushButton#gitCommitButton:disabled {
            background: @disabledSurface; color: @disabledForeground; border-color: @border;
        }
        QLabel#scmNotice { color: @error; }
        QLabel#extensionsEmpty { color: @muted; }
        QPlainTextEdit#gitCommitMessage {
            background: @input; color: @foreground; border: 1px solid @inputBorder;
            border-radius: 2px; padding: 5px; font-size: 13px;
            selection-background-color: @textSelectionBackground;
        }
        QPlainTextEdit#gitCommitMessage:focus { border-color: @focus; }
        QTreeWidget#gitChanges, QTreeWidget#gitHistory, QListWidget#nativeExtensions {
            background: @sidebar; color: @foreground; border: 0; outline: 0;
            font-size: 13px; padding: 0;
        }
        QTreeWidget#gitChanges::item { height: 22px; padding: 0; border: 0; }
        QTreeWidget#gitChanges::item:hover { background: @listHoverBackground; }
        QTreeWidget#gitChanges::item:selected { background: @listSelectionBackground; color: @listSelectionForeground; }
        QLabel#extensionsInstalled { font-weight: 600; font-size: 11px; padding: 6px 12px; }
        QWidget#sourceControlToolbar QToolButton, QWidget#extensionsToolbar QToolButton {
            padding: 0; min-width: 24px; min-height: 24px;
            background: transparent; border: 0; border-radius: 3px;
        }
        QWidget#sourceControlToolbar QToolButton:hover, QWidget#extensionsToolbar QToolButton:hover { background: @hover; }
    )QSS"),
                            tokens);
}
} // namespace litecode::ui
