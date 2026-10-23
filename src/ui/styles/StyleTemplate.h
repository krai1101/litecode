#pragma once

#include <QString>

class QColor;

namespace litecode::ui {

struct ThemeTokens;

[[nodiscard]] QString applyThemeTokens(QString styleSheet, const ThemeTokens& tokens);
[[nodiscard]] QString styleColor(const QColor& color);

} // namespace litecode::ui
