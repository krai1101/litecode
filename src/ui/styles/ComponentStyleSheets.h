#pragma once

#include <QString>

namespace litecode::ui {

struct ThemeTokens;

class ComponentStyleSheets final {
  public:
    [[nodiscard]] static QString workbench(const ThemeTokens& tokens);
    [[nodiscard]] static QString explorer(const ThemeTokens& tokens);
    [[nodiscard]] static QString editorChrome(const ThemeTokens& tokens);
    [[nodiscard]] static QString terminalAndPanel(const ThemeTokens& tokens);
    [[nodiscard]] static QString dialogsMenusAndTooltips(const ThemeTokens& tokens);
    [[nodiscard]] static QString scrollbarsAndStatus(const ThemeTokens& tokens);
};

} // namespace litecode::ui
