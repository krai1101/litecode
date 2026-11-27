#include "ui/extensions/NativeInlineCode.h"
#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
namespace litecode::ui {
void paintInlineCodeBackgrounds(QPainter& painter, QTextDocument& document, const QPointF& offset,
                                const QColor& background) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        const auto* layout = block.layout();
        if (!layout)
            continue;
        const auto origin = document.documentLayout()->blockBoundingRect(block).topLeft() + offset;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid() ||
                !fragment.charFormat().boolProperty(NativeInlineCodeProperty))
                continue;
            const int start = fragment.position() - block.position();
            const int end = start + fragment.length();
            const QFontMetricsF metrics(fragment.charFormat().font());
            const auto firstLine = layout->lineForTextPosition(start);
            if (!firstLine.isValid())
                continue;
            for (int index = firstLine.lineNumber(); index < layout->lineCount(); ++index) {
                const auto line = layout->lineAt(index);
                if (line.textStart() >= end)
                    break;
                const int from = qMax(start, line.textStart());
                const int to = qMin(end, line.textStart() + line.textLength());
                if (from >= to)
                    continue;
                const auto x1 = line.cursorToX(from);
                const auto x2 = line.cursorToX(to);
                const QRectF rect(origin.x() + qMin(x1, x2) - 2,
                                  origin.y() + line.y() + line.ascent() - metrics.ascent() - 1,
                                  qAbs(x2 - x1) + 4, metrics.height() + 2);
                painter.drawRoundedRect(rect, 5, 5);
            }
        }
    }
    painter.restore();
}
} // namespace litecode::ui
