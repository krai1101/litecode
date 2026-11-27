#include "ui/components/ElidingButton.h"
#include <QStyleOptionButton>
#include <QStylePainter>
namespace litecode::ui::components {
QSize ElidingButton::minimumSizeHint() const {
    auto size = Button::minimumSizeHint();
    size.setWidth(0);
    return size;
}
void ElidingButton::paintEvent(QPaintEvent*) {
    QStyleOptionButton option;
    initStyleOption(&option);
    const auto original = option.text;
    option.text.clear();
    QStylePainter painter(this);
    painter.drawControl(QStyle::CE_PushButton, option);
    const auto area = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
    const QFontMetrics metrics(font());
    auto shown = original;
    if (metrics.horizontalAdvance(shown) > area.width()) {
        const int slash = qMax(original.lastIndexOf('/'), original.lastIndexOf('\\'));
        const auto name = original.mid(slash + 1);
        const int directoryWidth = area.width() - metrics.horizontalAdvance(name + ' ');
        if (slash >= 0 && directoryWidth > metrics.horizontalAdvance(QChar(0x2026)))
            shown = metrics.elidedText(original.left(slash + 1), Qt::ElideRight, directoryWidth) +
                    ' ' + name;
        else
            shown = metrics.elidedText(slash >= 0 ? name : original, Qt::ElideRight,
                                       qMax(0, area.width()));
    }
    painter.drawItemText(area, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine,
                         option.palette, isEnabled(), shown, QPalette::ButtonText);
}
}
