#include "ui/explorer/ExplorerTreeView.h"

#include "ui/FileIconTheme.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"

#include <QApplication>
#include <QBrush>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStyledItemDelegate>

#include <algorithm>

namespace litecode::ui {

class ExplorerItemDelegate final : public QStyledItemDelegate {
  public:
    explicit ExplorerItemDelegate(ExplorerTreeView& tree)
        : QStyledItemDelegate(&tree), tree_(tree) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&,
                          const QModelIndex& index) const override {
        if (!tree_.isInlineEditing(index))
            return nullptr;
        auto* editor = new components::InlineInput(parent);
        editor->setObjectName(QStringLiteral("explorerInlineEditor"));
        editor->setAccessibleName(tree_.tr("Explorer item name"));
        tree_.registerInlineEditor(editor);
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        if (auto* input = qobject_cast<QLineEdit*>(editor))
            input->setText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget*, QAbstractItemModel*, const QModelIndex&) const override {}

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex&) const override {
        QStyleOptionFrame frame;
        frame.initFrom(editor);
        frame.rect = QRect(0, 0, option.rect.width(), option.rect.height());
        const int contentInset =
            editor->style()->subElementRect(QStyle::SE_LineEditContents, &frame, editor).left();
        const int x = option.rect.left() + ExplorerTreeView::itemTextInset - contentInset;
        const int right = std::min(option.rect.right(), tree_.contentRightEdge());
        editor->setGeometry(x, option.rect.top(), std::max(0, right - x + 1), option.rect.height());
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == tree_.inlineEditor_) {
            if (event->type() == QEvent::KeyPress) {
                const auto* key = static_cast<QKeyEvent*>(event);
                if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                    tree_.acceptInlineEditor();
                    return true;
                }
                if (key->key() == Qt::Key_Escape) {
                    tree_.cancelInlineEditor();
                    return true;
                }
            } else if (event->type() == QEvent::FocusOut) {
                tree_.cancelInlineEditor();
            }
        }
        return QStyledItemDelegate::eventFilter(watched, event);
    }

  private:
    ExplorerTreeView& tree_;
};

ExplorerTreeView::ExplorerTreeView(ExplorerFileSystemModel& model, QWidget* parent)
    : QTreeView(parent), files_(model) {
    setModel(&model);
    setItemDelegate(new ExplorerItemDelegate(*this));
    connect(&model, &ExplorerFileSystemModel::temporaryEntryInvalidated, this, [this] {
        inlineEditIndex_ = QPersistentModelIndex{};
        inlineEditor_.clear();
        emit inlineEditCancelled();
        viewport()->update();
    });
}

void ExplorerTreeView::beginCreateEditor(const QModelIndex& parentIndex, bool directory) {
    endInlineEditor();
    if (parentIndex.isValid() && parentIndex != rootIndex())
        expand(parentIndex);
    inlineEditIndex_ = QPersistentModelIndex(files_.insertTemporaryEntry(parentIndex, directory));
    scrollTo(inlineEditIndex_);
    openPersistentEditor(inlineEditIndex_);
    if (inlineEditor_)
        inlineEditor_->setFocus(Qt::ShortcutFocusReason);
}

void ExplorerTreeView::beginRenameEditor(const QModelIndex& index, const QString& initialName) {
    endInlineEditor();
    inlineEditIndex_ = QPersistentModelIndex(index);
    scrollTo(index);
    openPersistentEditor(index);
    if (inlineEditor_) {
        inlineEditor_->setText(initialName);
        const int suffixPosition = initialName.lastIndexOf(QLatin1Char('.'));
        if (suffixPosition > 0)
            inlineEditor_->setSelection(0, suffixPosition);
        else
            inlineEditor_->selectAll();
        inlineEditor_->setFocus(Qt::ShortcutFocusReason);
    }
}

void ExplorerTreeView::endInlineEditor() {
    const QModelIndex index = inlineEditIndex_;
    inlineEditIndex_ = QPersistentModelIndex{};
    if (index.isValid())
        closePersistentEditor(index);
    inlineEditor_.clear();
    files_.removeTemporaryEntry();
    viewport()->update();
}

void ExplorerTreeView::setInlineEditorError(const QString& message) {
    if (!inlineEditor_)
        return;
    components::setInputState(*inlineEditor_, components::InputState::Error);
    inlineEditor_->setToolTip(message);
    inlineEditor_->selectAll();
    inlineEditor_->setFocus(Qt::OtherFocusReason);
}

bool ExplorerTreeView::hasInlineEditor() const { return !inlineEditor_.isNull(); }

bool ExplorerTreeView::isInlineEditing(const QModelIndex& index) const {
    return inlineEditIndex_.isValid() && inlineEditIndex_ == index;
}

void ExplorerTreeView::registerInlineEditor(QLineEdit* editor) {
    inlineEditor_ = editor;
    connect(editor, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (files_.isTemporaryEntry(inlineEditIndex_))
            files_.setData(inlineEditIndex_, text, Qt::EditRole);
    });
}

void ExplorerTreeView::acceptInlineEditor() {
    if (inlineEditor_)
        emit inlineEditAccepted(inlineEditor_->text().trimmed());
}

void ExplorerTreeView::cancelInlineEditor() { emit inlineEditCancelled(); }

int ExplorerTreeView::contentRightEdge() const {
    const QScrollBar* scrollBar = verticalScrollBar();
    const int reservedGutter =
        scrollBar->isVisible()
            ? 0
            : scrollBar->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, scrollBar);
    return std::max(0, viewport()->rect().right() - reservedGutter);
}

void ExplorerTreeView::mouseMoveEvent(QMouseEvent* event) {
    const QPersistentModelIndex previous = hoveredIndex_;
    hoveredIndex_ = QPersistentModelIndex(indexAt(event->position().toPoint()));
    QTreeView::mouseMoveEvent(event);
    if (previous != hoveredIndex_)
        viewport()->update();
}

void ExplorerTreeView::leaveEvent(QEvent* event) {
    hoveredIndex_ = QPersistentModelIndex{};
    viewport()->update();
    QTreeView::leaveEvent(event);
}

void ExplorerTreeView::drawBranches(QPainter* painter, const QRect& rect,
                                    const QModelIndex& index) const {
    const bool temporaryDirectory = files_.isTemporaryEntry(index) && files_.isDir(index);
    if (!files_.hasChildren(index) && !temporaryDirectory) {
        if (!files_.isDir(index)) {
            const QRect iconRect(rect.right() - fileIconWidth,
                                 rect.center().y() - fileIconHeight / 2, fileIconWidth,
                                 fileIconHeight);
            const QString iconPath = files_.isTemporaryEntry(index)
                                         ? index.data(Qt::DisplayRole).toString()
                                         : files_.filePath(index);
            (void)paintFileIcon(*painter, iconRect, iconPath);
        }
        return;
    }
    const bool dark = qApp->property("litecodeDarkTheme").toBool();
    constexpr int glyphExtent = 16;
    const QRect iconRect(rect.right() - glyphExtent, rect.center().y() - glyphExtent / 2,
                         glyphExtent, glyphExtent);
    const QString iconPath = isExpanded(index) ? QStringLiteral(":/icons/chevron-down.svg")
                                               : QStringLiteral(":/icons/chevron-right.svg");
    themedIcon(iconPath, dark).paint(painter, iconRect, Qt::AlignCenter);
}

void ExplorerTreeView::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    QColor rowBackground;
    const bool dark = qApp->property("litecodeDarkTheme").toBool();
    const ThemeTokens theme = Theme::tokens(dark);
    const bool temporary = files_.isTemporaryEntry(index);
    if (!temporary && currentIndex() == index) {
        rowBackground = theme.listSelectionBackground;
    } else if (!temporary && hoveredIndex_ == index) {
        rowBackground = theme.listHoverBackground;
    }
    if (rowBackground.isValid())
        painter->fillRect(QRect(0, option.rect.y(), contentRightEdge() + 1, option.rect.height()),
                          rowBackground);

    QRect itemRect = visualRect(index);
    itemRect.setTop(option.rect.top());
    itemRect.setHeight(option.rect.height());
    itemRect.setRight(viewport()->width() - 1);
    const QRect branchRect(0, option.rect.top(), std::max(0, itemRect.left()),
                           option.rect.height());
    if (branchRect.width() > 0)
        drawBranches(painter, branchRect, index);
    if (isInlineEditing(index))
        return;

    QFont itemFont = font();
    const QVariant fontData = model()->data(index, Qt::FontRole);
    if (fontData.canConvert<QFont>())
        itemFont = qvariant_cast<QFont>(fontData);
    QColor textColor = palette().color(QPalette::Text);
    const QVariant foregroundData = model()->data(index, Qt::ForegroundRole);
    if (foregroundData.canConvert<QBrush>())
        textColor = qvariant_cast<QBrush>(foregroundData).color();
    painter->save();
    painter->setFont(itemFont);
    painter->setPen(textColor);
    const QRect textRect = itemRect.adjusted(itemTextInset, 0, -itemTextInset, 0);
    const QString label = model()->data(index, Qt::DisplayRole).toString();
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(itemFont).elidedText(label, Qt::ElideRight, textRect.width()));
    painter->restore();
}

} // namespace litecode::ui
