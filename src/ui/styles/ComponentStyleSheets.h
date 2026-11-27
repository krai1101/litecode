#pragma once

#include <QString>
class QColor;

namespace litecode::ui {

struct ThemeTokens;

class ComponentStyleSheets final {
  public:
    [[nodiscard]] static QString workbench(const ThemeTokens& tokens);
    [[nodiscard]] static QString sourceControl(const ThemeTokens& tokens);
    [[nodiscard]] static QString runDebug(const ThemeTokens& tokens);
    [[nodiscard]] static QString diffEditor(const ThemeTokens& tokens);
    [[nodiscard]] static QString explorer(const ThemeTokens& tokens);
    [[nodiscard]] static QString editorChrome(const ThemeTokens& tokens);
    [[nodiscard]] static QString terminalAndPanel(const ThemeTokens& tokens);
    [[nodiscard]] static QString dialogsMenusAndTooltips(const ThemeTokens& tokens);
    [[nodiscard]] static QString scrollbarsAndStatus(const ThemeTokens& tokens);
    [[nodiscard]] static QString iconLabelButtonColors(const QColor& foreground,
        const QColor& background, const QColor& hover);
};

} // namespace litecode::ui
