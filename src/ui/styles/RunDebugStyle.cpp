#include "ui/styles/ComponentStyleSheets.h"
#include "ui/Theme.h"
#include "ui/styles/StyleTemplate.h"
namespace litecode::ui {
QString ComponentStyleSheets::runDebug(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QWidget#runDebugPanel, QWidget#runDebugToolbar {
            background: @sidebar; color: @foreground;
        }
        QWidget#runDebugToolbar QToolButton {
            padding: 0; min-width: 24px; min-height: 24px;
            background: transparent; border: 0; border-radius: 3px;
        }
        QWidget#runDebugToolbar QToolButton:hover { background: @hover; }
        QWidget#debugLaunchGroup {
            background: @input; border: 1px solid @inputBorder; border-radius: 3px;
        }
        QWidget#debugLaunchGroup QToolButton {
            border: 0; border-right: 1px solid @inputBorder;
            border-radius: 0; min-width: 24px; min-height: 22px;
        }
        QComboBox#debugConfigurationPicker {
            background: transparent; border: 0; border-radius: 0;
            min-height: 0; max-height: 22px; padding: 0 4px;
            font-size: 12px;
        }
        QToolButton#debugSectionHeader {
            min-height: 24px; padding: 0 4px; font-weight: 400;
        }
        QSplitter#debugSections::handle { background: @border; }
        QWidget#debugWelcomeDivider { background: @border; }
        QTreeWidget#debugVariables, QTreeWidget#debugWatch,
        QTreeWidget#debugCallStack, QTreeWidget#debugBreakpoints {
            background: @sidebar; color: @foreground; border: 0; outline: 0;
        }
        QTreeWidget#debugVariables::item,
        QTreeWidget#debugWatch::item,
        QTreeWidget#debugCallStack::item,
        QTreeWidget#debugBreakpoints::item { height: 22px; padding-top: 0; padding-bottom: 0; }
        QTreeWidget#debugBreakpoints::indicator {
            width: 16px; height: 16px; border: 1px solid @inputBorder;
            border-radius: 3px; background: @sidebar;
        }
        QTreeWidget#debugBreakpoints::indicator:checked { image: url(:/icons/@debugCheckIcon); }
        QToolButton#debugCollapseVariablesButton,
        QToolButton#debugAddWatchButton {
            background: @sidebar; border: 0; min-width: 22px; min-height: 22px;
        }
        QToolButton#debugCollapseVariablesButton:hover,
        QToolButton#debugAddWatchButton:hover { background: @hover; }
        QLabel#debugCallStackState {
            background: @listSelectionBackground; color: @foreground; font-size: 10px;
            border-radius: 3px; padding: 2px 5px;
        }
        QLabel#runDebugNotice { color: @error; background: transparent; }
    )QSS"), tokens).replace("@debugCheckIcon", tokens.sidebarSurface.lightness() < 128
        ? "check.svg" : "check-neutral.svg");
}
} // namespace litecode::ui
