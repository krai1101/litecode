#pragma once

#include <QIcon>
#include <QRect>
#include <QString>

class QPainter;

namespace litecode::ui {

[[nodiscard]] bool setiIconThemeAvailable();
[[nodiscard]] QIcon fileIconForPath(const QString& path, bool directory = false);
// Paint Seti glyphs directly into the current paint device to preserve native font hinting.
// Returns false only when the generic SVG fallback was painted instead.
[[nodiscard]] bool paintFileIcon(QPainter& painter, const QRect& target, const QString& path,
                                 bool directory = false);

} // namespace litecode::ui
