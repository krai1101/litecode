#include "ui/styles/ComponentStyleSheets.h"
#include "ui/styles/StyleTemplate.h"
namespace litecode::ui {
QString ComponentStyleSheets::diffEditor(const ThemeTokens& tokens) {
    return applyThemeTokens(QStringLiteral(R"QSS(
        QWidget#gitDiffEditor { background: @editor; color: @foreground; }
        QWidget#diffToolbar, QLabel#diffPaneLabel {
            background: @panel; color: @foreground;
            border-bottom: 1px solid @border;
        }
        QWidget#diffPadding { background: @panel; }
        QWidget#diffToolbar QLabel { background: transparent; color: @foreground; }
        QScrollBar#diffVerticalScroll { width: 12px; }
    )QSS"),
                            tokens);
}
} // namespace litecode::ui
