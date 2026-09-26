#include "ui/TerminalSessionDelegate.h"

#include "ui/Theme.h"
#include "ui/ThemedIcon.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionViewItem>

namespace litecode::ui {

TerminalSessionDelegate::TerminalSessionDelegate(std::function<void(int)> removeSession,
                                                 QObject* parent)
    : QStyledItemDelegate(parent), removeSession_(std::move(removeSession)) {}

void TerminalSessionDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    const bool dark = qApp->property("litecodeDarkTheme").toBool();
    const ThemeTokens theme = Theme::tokens(dark);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    painter->save();
    if (selected) {
        painter->fillRect(option.rect, theme.listSelectionBackground);
    }

    const QRect iconRect(option.rect.left() + 7, option.rect.center().y() - 8, 16, 16);
    qvariant_cast<QIcon>(index.data(Qt::DecorationRole))
        .paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal,
               selected ? QIcon::On : QIcon::Off);
    const int actionWidth = selected || hovered ? 28 : 6;
    const QRect textRect(option.rect.left() + 29, option.rect.top(),
                         option.rect.width() - 29 - actionWidth, option.rect.height());
    painter->setFont(option.font);
    painter->setPen(theme.foreground);
    painter->drawText(
        textRect, Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, textRect.width()));
    if (selected || hovered) {
        const QRect actionHitRect(option.rect.right() - 27, option.rect.center().y() - 11, 22, 22);
        if (option.widget && actionHitRect.contains(option.widget->mapFromGlobal(QCursor::pos()))) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme.terminalSessionActionHover);
            painter->drawRoundedRect(actionHitRect, 4, 4);
        }
        const QRect actionRect(actionHitRect.center().x() - 8, actionHitRect.center().y() - 8, 16,
                               16);
        themedIcon(QStringLiteral(":/icons/clear.svg"), dark)
            .paint(painter, actionRect, Qt::AlignCenter, QIcon::Normal);
    }
    painter->restore();
}

bool TerminalSessionDelegate::editorEvent(QEvent* event, QAbstractItemModel*,
                                          const QStyleOptionViewItem& option,
                                          const QModelIndex& index) {
    if (event->type() == QEvent::MouseMove) {
        if (option.widget) {
            const_cast<QWidget*>(option.widget)->update(option.rect);
        }
        return false;
    }
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease) {
        return false;
    }
    const auto* mouse = static_cast<QMouseEvent*>(event);
    const QRect actionRect(option.rect.right() - 27, option.rect.center().y() - 11, 22, 22);
    if (mouse->button() != Qt::LeftButton) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        if (!actionRect.contains(mouse->position().toPoint()))
            return false;
        pressedKillIndex_ = index;
        // Consume the press so clicking an inactive terminal's action does not select it first.
        return true;
    }
    const bool activate =
        pressedKillIndex_ == index && actionRect.contains(mouse->position().toPoint());
    pressedKillIndex_ = QPersistentModelIndex{};
    if (activate)
        removeSession_(index.row());
    return activate;
}

} // namespace litecode::ui
