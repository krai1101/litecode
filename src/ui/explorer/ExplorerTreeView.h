#pragma once

#include "ui/explorer/ExplorerFileSystemModel.h"

#include <QPointer>
#include <QTreeView>

class QLineEdit;

namespace litecode::ui {

class ExplorerTreeView final : public QTreeView {
    Q_OBJECT

  public:
    explicit ExplorerTreeView(ExplorerFileSystemModel& model, QWidget* parent = nullptr);

    void beginCreateEditor(const QModelIndex& parent, bool directory);
    void beginRenameEditor(const QModelIndex& index, const QString& initialName);
    void endInlineEditor();
    void setInlineEditorError(const QString& message);
    [[nodiscard]] bool hasInlineEditor() const;
    [[nodiscard]] bool isInlineEditing(const QModelIndex& index) const;

    static constexpr int fileIconWidth = 16;
    static constexpr int fileIconHeight = 22;
    static constexpr int itemTextInset = 4;

  signals:
    void inlineEditAccepted(const QString& name);
    void inlineEditCancelled();

  protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override;
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

  private:
    friend class ExplorerItemDelegate;
    void registerInlineEditor(QLineEdit* editor);
    void acceptInlineEditor();
    void cancelInlineEditor();
    [[nodiscard]] int contentRightEdge() const;

    ExplorerFileSystemModel& files_;
    QPersistentModelIndex hoveredIndex_;
    QPersistentModelIndex inlineEditIndex_;
    QPointer<QLineEdit> inlineEditor_;
};

} // namespace litecode::ui
