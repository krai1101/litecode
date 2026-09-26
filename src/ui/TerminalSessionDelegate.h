#pragma once

#include <QPersistentModelIndex>
#include <QStyledItemDelegate>

#include <functional>

namespace litecode::ui {

class TerminalSessionDelegate final : public QStyledItemDelegate {
  public:
    explicit TerminalSessionDelegate(std::function<void(int)> removeSession,
                                     QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

  private:
    std::function<void(int)> removeSession_;
    QPersistentModelIndex pressedKillIndex_;
};

} // namespace litecode::ui
