#pragma once
#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

namespace litecode::ui {
// Optional tab-separated trailing metadata uses the same row and selection as
// its title. Ordinary list items retain Qt's standard rendering.
class NativeListDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const auto text = index.data(Qt::DisplayRole).toString();
        const auto split = text.indexOf('\t');
        if (split < 0) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        item.text.clear();
        const auto* style = item.widget ? item.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
        const auto rect = item.rect.adjusted(0, 0, -6, 0);
        const auto trailing = text.mid(split + 1);
        auto metadataFont = item.font;
        if (metadataFont.pixelSize() > 12)
            metadataFont.setPixelSize(12);
        const QFontMetrics metadataMetrics(metadataFont);
        const int trailingWidth = metadataMetrics.horizontalAdvance(trailing);
        const auto role =
            item.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text;
        painter->save();
        painter->setFont(item.font);
        painter->setPen(item.palette.color(role));
        painter->setFont(metadataFont);
        painter->drawText(rect, Qt::AlignRight | Qt::AlignVCenter, trailing);
        painter->setFont(item.font);
        const auto titleRect = rect.adjusted(0, 0, -trailingWidth - 12, 0);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          item.fontMetrics.elidedText(text.left(split), Qt::ElideRight,
                                                      qMax(0, titleRect.width())));
        painter->restore();
    }
};
} // namespace litecode::ui
