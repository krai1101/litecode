#include "ui/styles/ComponentStyleSheets.h"
#include <QColor>

namespace litecode::ui {
QString ComponentStyleSheets::iconLabelButtonColors(const QColor& foreground,
                                                    const QColor& background, const QColor& hover) {
    // Only typed, validated colors enter stylesheet text, never extension CSS.
    const auto css = [](const QColor& color) {
        return color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("transparent");
    };
    return QStringLiteral("QToolButton { color: %1; background: %2; } "
                          "QToolButton:hover { background: %3; }")
        .arg(css(foreground), css(background), css(hover));
}
} // namespace litecode::ui
