#pragma once
#include <QTextFormat>
class QPainter;
class QTextDocument;
namespace litecode::ui {
inline constexpr int NativeInlineCodeProperty = QTextFormat::UserProperty + 29;
// Paint behind selectable text; inline code remains ordinary document text.
void paintInlineCodeBackgrounds(QPainter& painter, QTextDocument& document, const QPointF& offset,
                                const QColor& background);
} // namespace litecode::ui
