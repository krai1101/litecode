#include "ui/Theme.h"
#include "ui/components/Controls.h"
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QtMath>

namespace litecode::ui::components {
void Menu::paintEvent(QPaintEvent* event) {
    if (property("uiMenuStyle") != "popover") {
        QMenu::paintEvent(event);
        return;
    }
    const auto theme = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
    const auto ratio = devicePixelRatioF();
    const qreal pixel = 1.0 / ratio;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath surface;
    surface.addRoundedRect(QRectF(rect()).adjusted(pixel / 2, pixel / 2, -pixel / 2, -pixel / 2),
                           14, 14);
    auto border = theme.foreground;
    border.setAlpha(30);
    painter.setPen(QPen(border, pixel));
    painter.setBrush(theme.menuSurface);
    painter.drawPath(surface);
    painter.setClipPath(surface);
    auto body = font();
    body.setWeight(QFont::Normal);
    painter.setFont(body);
    for (auto* action : actions()) {
        if (!action->isVisible())
            continue;
        const auto row = actionGeometry(action);
        if (row.isEmpty())
            continue;
        if (action->isSeparator()) {
            auto color = theme.foreground;
            color.setAlpha(20);
            const qreal y = (qFloor(row.center().y() * ratio) + 0.5) / ratio;
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(color, pixel));
            painter.drawLine(QPointF(row.left(), y), QPointF(row.right(), y));
            painter.setRenderHint(QPainter::Antialiasing);
            continue;
        }
        if (action == activeAction() && action->isEnabled()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme.menuSelectionBackground);
            painter.drawRoundedRect(row, 4, 4);
        }
        const auto color = action->isEnabled() ? theme.foreground : theme.disabledForeground;
        const auto visual = [&](const QRect& logical) {
            return QStyle::visualRect(layoutDirection(), row, logical);
        };
        const auto source = action->property("uiIconSource").toString();
        if (!source.isEmpty() || !action->icon().isNull()) {
            const QIcon icon = source.isEmpty() ? action->icon() : QIcon(source);
            auto pixmap = icon.pixmap(QSize(14, 14), ratio);
            QPainter tint(&pixmap);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(pixmap.rect(), color);
            tint.end();
            painter.drawPixmap(visual(QRect(row.left(), row.center().y() - 7, 14, 14)), pixmap);
        }
        painter.setPen(color);
        const auto text = action->text().section('\t', 0, 0);
        const auto textRect = visual(row.adjusted(22, 0, -18, 0));
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeading,
                         QFontMetrics(body).elidedText(text, Qt::ElideRight, textRect.width()));
        if (action->menu()) {
            const auto arrow = visual(QRect(row.right() - 6, row.center().y() - 4, 4, 8));
            const qreal direction = layoutDirection() == Qt::RightToLeft ? -1 : 1;
            const QPointF center = arrow.center();
            QPainterPath chevron;
            chevron.moveTo(center + QPointF(-2 * direction, -4));
            chevron.lineTo(center + QPointF(2 * direction, 0));
            chevron.lineTo(center + QPointF(-2 * direction, 4));
            painter.setPen(QPen(color, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(chevron);
        }
    }
}
} // namespace litecode::ui::components
