#include "ui/components/CssColor.h"
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <optional>
namespace litecode::ui::components {
namespace {
std::optional<double> number(QString value, double scale) {
    const bool percent = value.endsWith('%'); if (percent) value.chop(1);
    bool valid = false; const auto result = value.toDouble(&valid);
    if (!valid || !std::isfinite(result)) return {};
    return std::clamp(percent ? result / 100 : result / scale, 0.0, 1.0);
}
}
QColor cssColor(const QString& input) {
    const auto value = input.trimmed();
    if (value.startsWith('#')) {
        const auto hex = value.mid(1);
        if (hex.size() == 4) return QColor("#" + hex.mid(3, 1).repeated(2) + hex.mid(0, 1).repeated(2) + hex.mid(1, 1).repeated(2) + hex.mid(2, 1).repeated(2));
        if (hex.size() == 8) return QColor("#" + hex.right(2) + hex.left(6));
        if (hex.size() == 3 || hex.size() == 6) return QColor(value);
        return {};
    }
    static const QRegularExpression function("^(rgb|rgba|hsl|hsla)\\((.*)\\)$", QRegularExpression::CaseInsensitiveOption);
    const auto match = function.match(value);
    if (!match.hasMatch()) return QColor(value);
    const auto kind = match.captured(1).toLower();
    const auto parts = match.captured(2).split(QRegularExpression("[\\s,/]+"), Qt::SkipEmptyParts);
    if (parts.size() != 3 && parts.size() != 4) return {};
    const auto alpha = parts.size() == 4 ? number(parts.at(3), 1) : std::optional<double>(1);
    if (!alpha) return {};
    if (kind.startsWith("rgb")) {
        const auto red = number(parts.at(0), 255), green = number(parts.at(1), 255), blue = number(parts.at(2), 255);
        if (!red || !green || !blue) return {};
        return QColor::fromRgbF(static_cast<float>(*red), static_cast<float>(*green), static_cast<float>(*blue), static_cast<float>(*alpha));
    }
    auto hueText = parts.at(0); double multiplier = 1;
    if (hueText.endsWith("grad")) { hueText.chop(4); multiplier = 0.9; }
    else if (hueText.endsWith("deg")) hueText.chop(3);
    else if (hueText.endsWith("rad")) { hueText.chop(3); multiplier = 180 / std::acos(-1.0); }
    else if (hueText.endsWith("turn")) { hueText.chop(4); multiplier = 360; }
    bool valid = false; auto hue = hueText.toDouble(&valid) * multiplier;
    if (!valid || !std::isfinite(hue) || !parts.at(1).endsWith('%') || !parts.at(2).endsWith('%')) return {};
    hue = std::fmod(hue, 360); if (hue < 0) hue += 360;
    const auto saturation = number(parts.at(1), 1), lightness = number(parts.at(2), 1);
    if (!saturation || !lightness) return {};
    return QColor::fromHslF(static_cast<float>(hue / 360), static_cast<float>(*saturation), static_cast<float>(*lightness), static_cast<float>(*alpha));
}
}
