#include "ui/DiffEditorWidget.h"
#include "editor/ScintillaEditor.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"
#include "ui/styles/ComponentStyleSheets.h"
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>

namespace litecode::ui {
namespace {
class DiffPathLabel final : public QLabel {
  public:
    explicit DiffPathLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setToolTip(text);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        const auto bounds = contentsRect();
        painter.drawText(bounds, Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(text(), Qt::ElideMiddle, bounds.width()));
    }
};
} // namespace
DiffEditorWidget::DiffEditorWidget(const workspace::GitComparison& comparison, QWidget* parent,
                                   bool openFileAvailable)
    : QWidget(parent), comparison_(comparison) {
    setObjectName("gitDiffEditor");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName("diffToolbar");
    toolbar->setFixedHeight(35);
    auto* tools = new QHBoxLayout(toolbar);
    tools->setContentsMargins(12, 0, 8, 0);
    auto* title = new DiffPathLabel(comparison.path, toolbar);
    title->setToolTip(comparison.path);
    tools->addWidget(title, 1);
    counter_ = new QLabel(toolbar);
    tools->addWidget(counter_);
    auto button = [&](const QString& name, const QString& icon, const QString& tooltip,
                      auto action) {
        auto* control = new components::IconButton(toolbar);
        control->setObjectName(name);
        control->setAutoRaise(true);
        control->setFixedSize(28, 28);
        control->setIconSize(QSize(16, 16));
        setThemedIcon(control, icon);
        control->setToolTip(tooltip);
        tools->addWidget(control);
        connect(control, &QToolButton::clicked, this, action);
        return control;
    };
    button("previousDiff", ":/icons/arrow-up.svg", tr("Previous change (Shift+F7)"),
           [this] { navigate(-1); });
    button("nextDiff", ":/icons/arrow-down.svg", tr("Next change (F7)"), [this] { navigate(1); });
    revert_ = button("revertDiffChange", ":/icons/arrow-left.svg", tr("Revert current change"),
                     [this] { revertCurrentChange(); });
    revert_->setEnabled(false);
    revert_->setVisible(!comparison.staged && comparison.modifiedExists && !comparison.binary);
    button("openDiffFile", ":/icons/arrow-right.svg", tr("Open file"), [this] {
        emit openFileRequested(workingPath());
    })->setVisible(openFileAvailable);
    layout->addWidget(toolbar);
    if (comparison.binary) {
        auto* notice =
            new QLabel(tr("Binary files cannot be displayed in the text diff editor."), this);
        notice->setAlignment(Qt::AlignCenter);
        layout->addWidget(notice, 1);
        setDarkTheme(qApp->property("litecodeDarkTheme").toBool());
        return;
    }
    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto* splitter = new QSplitter(Qt::Horizontal, body);
    splitter_ = splitter;
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);
    auto pane = [&](editor::ScintillaEditor*& control, QWidget*& gap, QLabel*& label,
                    const QString& text) {
        auto* container = new QWidget(splitter);
        container->setMinimumWidth(100);
        auto* column = new QVBoxLayout(container);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        label = new DiffPathLabel(text, container);
        label->setObjectName("diffPaneLabel");
        label->setFixedHeight(28);
        label->setContentsMargins(12, 0, 0, 0);
        column->addWidget(label);
        gap = new QWidget(container);
        gap->setObjectName("diffPadding");
        gap->setFixedHeight(0);
        column->addWidget(gap);
        control = new editor::ScintillaEditor(container);
        control->setFilePath(comparison.path);
        control->setReadOnly(true);
        control->configureDiffView();
        control->setObjectName(control == original_ ? "diffOriginal" : "diffModified");
        column->addWidget(control, 1);
        for (auto* child : control->findChildren<QWidget*>())
            child->installEventFilter(this);
        control->installEventFilter(this);
        connect(control, &editor::ScintillaEditor::contextMenuRequested, this,
                [this, control](const QPoint& position) {
                    components::Menu menu(this);
                    if (control == modified_ && model_) {
                        menu.addAction(tr("Undo"), this, &DiffEditorWidget::undo);
                        menu.addAction(tr("Redo"), this, &DiffEditorWidget::redo);
                        menu.addSeparator();
                    }
                    auto* copy =
                        menu.addAction(tr("Copy"), control, &editor::ScintillaEditor::copy);
                    copy->setEnabled(!control->selectedText().isEmpty());
                    if (!control->isReadOnly()) {
                        menu.addAction(tr("Cut"), control, &editor::ScintillaEditor::cut);
                        menu.addAction(tr("Paste"), control, &editor::ScintillaEditor::paste);
                    }
                    menu.addAction(tr("Select All"), control, &editor::ScintillaEditor::selectAll);
                    menu.exec(control->mapToGlobal(position));
                });
        connect(control, &editor::ScintillaEditor::diffScrolled, this,
                [this, control](int row, int x) {
                    if (synchronizing_ || !scroll_)
                        return;
                    synchronizing_ = true;
                    (control == original_ ? modified_ : original_)->setHorizontalOffset(x);
                    synchronizing_ = false;
                    scroll_->setValue(row + (control == original_ ? originalTop_ : modifiedTop_));
                });
    };
    pane(original_, originalGap_, originalLabel_,
         comparison.originalPath + (comparison.historical
                                        ? " • " + (comparison.originalRevision.isEmpty()
                                                       ? tr("Empty tree")
                                                       : comparison.originalRevision.left(7))
                                    : comparison.staged ? tr(" • HEAD")
                                                        : tr(" • Index")));
    pane(modified_, modifiedGap_, modifiedLabel_,
         comparison.path + (comparison.historical
                                ? " • " + comparison.modifiedRevision.left(7) + tr(" (read-only)")
                            : comparison.staged ? tr(" • Index (read-only)")
                                                : tr(" • Working tree")));
    original_->setText(comparison.original);
    modified_->setText(comparison.modified);
    bodyLayout->addWidget(splitter, 1);
    inline_ = new editor::ScintillaEditor(body);
    inline_->setObjectName("diffInline");
    inline_->setFilePath(comparison.path);
    inline_->setReadOnly(true);
    inline_->configureDiffView();
    inline_->installEventFilter(this);
    for (auto* child : inline_->findChildren<QWidget*>())
        child->installEventFilter(this);
    bodyLayout->addWidget(inline_, 1);
    inline_->hide();
    connect(inline_, &editor::ScintillaEditor::diffScrolled, this, [this](int row, int) {
        if (inlineMode_ && !synchronizing_ && scroll_)
            scroll_->setValue(row);
    });
    scroll_ = new QScrollBar(Qt::Vertical, body);
    scroll_->setObjectName("diffVerticalScroll");
    bodyLayout->addWidget(scroll_);
    connect(scroll_, &QScrollBar::valueChanged, this, &DiffEditorWidget::scrollTo);
    layout->addWidget(body, 1);
    findBar_ = new QWidget(this);
    auto* findLayout = new QHBoxLayout(findBar_);
    findLayout->setContentsMargins(8, 4, 8, 4);
    findInput_ = new components::Input(findBar_);
    findInput_->setPlaceholderText(tr("Find"));
    findInput_->setMinimumWidth(0);
    findLayout->addWidget(findInput_, 1);
    replaceInput_ = new components::Input(findBar_);
    replaceInput_->setPlaceholderText(tr("Replace"));
    replaceInput_->setMinimumWidth(0);
    findInput_->installEventFilter(this);
    replaceInput_->installEventFilter(this);
    findLayout->addWidget(replaceInput_, 1);
    auto* next = new components::IconButton(findBar_);
    setThemedIcon(next, ":/icons/arrow-down.svg");
    next->setToolTip(tr("Find next"));
    findLayout->addWidget(next);
    auto* replace = new components::IconButton(findBar_);
    replace->setObjectName("diffReplaceNext");
    setThemedIcon(replace, ":/icons/arrow-right.svg");
    replace->setToolTip(tr("Replace next"));
    findLayout->addWidget(replace);
    auto* close = new components::IconButton(findBar_);
    setThemedIcon(close, ":/icons/close.svg");
    findLayout->addWidget(close);
    connect(close, &QToolButton::clicked, findBar_, &QWidget::hide);
    auto findNext = [this] {
        if (auto* editor = activeEditor())
            editor->findNext(findInput_->text(), false, false);
    };
    connect(next, &QToolButton::clicked, this, findNext);
    connect(findInput_, &QLineEdit::returnPressed, this, findNext);
    connect(replace, &QToolButton::clicked, this, [this] {
        if (model_ && !model_->isReadOnly())
            modified_->replaceNext(findInput_->text(), replaceInput_->text(), false, false);
    });
    layout->insertWidget(1, findBar_);
    findBar_->hide();
    updateTimer_ = new QTimer(this);
    updateTimer_->setSingleShot(true);
    updateTimer_->setInterval(120);
    connect(updateTimer_, &QTimer::timeout, this, &DiffEditorWidget::recompute);
    connect(modified_, &editor::ScintillaEditor::textEdited, this,
            [this](qint64 position, qint64 removed, const QByteArray& inserted,
                   const core::TextRange&) {
                if (mirroring_ || !model_)
                    return;
                revert_->setEnabled(false);
                if (!undoGroup_) {
                    undoGroup_ = true;
                    model_->beginUndoGroup();
                    QTimer::singleShot(0, this, &DiffEditorWidget::finishUndoGroup);
                }
                mirroring_ = true;
                model_->replaceBytes(position, removed, inserted);
                mirroring_ = false;
                updateTimer_->start();
            });
    setDarkTheme(qApp->property("litecodeDarkTheme").toBool());
    recompute();
}
QString DiffEditorWidget::workingPath() const {
    return QDir(comparison_.repository).filePath(comparison_.path);
}
DiffEditorWidget::~DiffEditorWidget() { finishUndoGroup(); }
void DiffEditorWidget::finishUndoGroup() {
    if (undoGroup_ && model_)
        model_->endUndoGroup();
    undoGroup_ = false;
}
editor::ScintillaEditor* DiffEditorWidget::workingEditor() const { return model_; }
editor::ScintillaEditor* DiffEditorWidget::activeEditor() const {
    if (!original_)
        return nullptr;
    return active_ ? active_.data() : modified_;
}
void DiffEditorWidget::bindWorkingEditor(editor::ScintillaEditor* model) {
    if (!modified_ || comparison_.staged || !comparison_.modifiedExists || !model || model_)
        return;
    model_ = model;
    mirroring_ = true;
    modified_->setText(model->text());
    mirroring_ = false;
    modified_->setReadOnly(model->isReadOnly());
    connect(model, &editor::ScintillaEditor::modificationChanged, this,
            &DiffEditorWidget::modificationChanged);
    emit modificationChanged(model->isModified());
    connect(model, &editor::ScintillaEditor::textChanged, this, [this] {
        if (!mirroring_ && model_ && modified_->text() != model_->text()) {
            mirroring_ = true;
            const auto selection = modified_->selectionByteRange();
            modified_->setText(model_->text());
            modified_->selectByteRange(selection.first, selection.second);
            mirroring_ = false;
        }
        updateTimer_->start();
    });
    connect(model, &QObject::destroyed, this, [this] { modified_->setReadOnly(true); });
    recompute();
}
void DiffEditorWidget::setEditorFont(const QString& family, int size, int weight) {
    if (!original_)
        return;
    original_->setEditorFont(family, size, weight);
    modified_->setEditorFont(family, size, weight);
    inline_->setEditorFont(family, size, weight);
    decorate();
}
void DiffEditorWidget::setDarkTheme(bool dark) {
    dark_ = dark;
    const auto t = Theme::tokens(dark);
    setStyleSheet(ComponentStyleSheets::diffEditor(t));
    refreshThemedIcons(this, dark);
    if (original_) {
        original_->setDarkTheme(dark);
        modified_->setDarkTheme(dark);
        inline_->setDarkTheme(dark);
        decorate();
    }
}
void DiffEditorWidget::recompute() {
    if (!original_)
        return;
    const auto a = original_->text(), b = modified_->text();
    const auto generation = ++generation_;
    auto* watcher = new QFutureWatcher<QVector<editor::DiffHunk>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, a, b] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != generation_ || original_->text() != a || modified_->text() != b)
            return;
        hunks_ = result;
        comparedModified_ = b;
        currentChange_ =
            hunks_.isEmpty() ? -1 : qBound(0, currentChange_, static_cast<int>(hunks_.size()) - 1);
        decorate();
    });
    watcher->setFuture(QtConcurrent::run([a, b] { return editor::compareLines(a, b); }));
}
void DiffEditorWidget::decorate() {
    if (!original_ || !scroll_)
        return;
    if (inlineMode_) {
        decorateInline();
        return;
    }
    QVector<int> oldLines, newLines;
    QVector<QPair<int, int>> oldGaps, newGaps;
    QVector<QPair<qint64, qint64>> oldSpans, newSpans;
    const auto a = original_->text().split('\n'), b = modified_->text().split('\n');
    QVector<qint64> aOffset{0}, bOffset{0};
    for (const auto& line : a)
        aOffset.push_back(aOffset.last() + line.size() + 1);
    for (const auto& line : b)
        bOffset.push_back(bOffset.last() + line.size() + 1);
    originalTop_ = modifiedTop_ = 0;
    int oldPadding = 0, newPadding = 0;
    changeRows_.clear();
    for (const auto& h : hunks_) {
        if (h.originalStart + h.originalCount > a.size() ||
            h.modifiedStart + h.modifiedCount > b.size())
            continue;
        changeRows_.append(h.originalStart + oldPadding);
        for (int i = 0; i < h.originalCount; ++i)
            oldLines.append(h.originalStart + i);
        for (int i = 0; i < h.modifiedCount; ++i)
            newLines.append(h.modifiedStart + i);
        const int delta = h.modifiedCount - h.originalCount;
        if (delta > 0) {
            if (h.originalStart + h.originalCount == 0)
                originalTop_ += delta;
            else
                oldGaps.append({h.originalStart + h.originalCount - 1, delta});
            oldPadding += delta;
        } else if (delta < 0) {
            if (h.modifiedStart + h.modifiedCount == 0)
                modifiedTop_ -= delta;
            else
                newGaps.append({h.modifiedStart + h.modifiedCount - 1, -delta});
            newPadding -= delta;
        }
        for (int i = 0; i < qMin(h.originalCount, h.modifiedCount); ++i) {
            const auto& old = a[h.originalStart + i];
            const auto& next = b[h.modifiedStart + i];
            int prefix = 0, suffix = 0;
            while (prefix < old.size() && prefix < next.size() && old[prefix] == next[prefix])
                ++prefix;
            // Expand to UTF-8 code-point boundaries.
            while (prefix > 0 && ((prefix < old.size() &&
                                   (static_cast<unsigned char>(old[prefix]) & 0xc0) == 0x80) ||
                                  (prefix < next.size() &&
                                   (static_cast<unsigned char>(next[prefix]) & 0xc0) == 0x80)))
                --prefix;
            while (suffix < old.size() - prefix && suffix < next.size() - prefix &&
                   old[old.size() - 1 - suffix] == next[next.size() - 1 - suffix])
                ++suffix;
            while (suffix > 0 &&
                   ((static_cast<unsigned char>(old[old.size() - suffix]) & 0xc0) == 0x80 ||
                    (static_cast<unsigned char>(next[next.size() - suffix]) & 0xc0) == 0x80))
                --suffix;
            oldSpans.append({aOffset[h.originalStart + i] + prefix, old.size() - prefix - suffix});
            newSpans.append({bOffset[h.modifiedStart + i] + prefix, next.size() - prefix - suffix});
        }
    }
    synchronizing_ = true;
    original_->setDiffDecorations(oldLines, oldGaps, oldSpans, false);
    modified_->setDiffDecorations(newLines, newGaps, newSpans, true);
    totalRows_ =
        qMax(static_cast<int>(a.size()) + oldPadding, static_cast<int>(b.size()) + newPadding);
    const int page = qMax(1, (original_->height() + originalGap_->height()) /
                                 qMax(1, original_->diffLineHeight()));
    scroll_->setPageStep(page);
    scroll_->setRange(0, qMax(0, totalRows_ - page));
    synchronizing_ = false;
    scrollTo(scroll_->value());
    counter_->setText(hunks_.isEmpty()
                          ? tr("No changes")
                          : tr("%1 of %2 changes").arg(currentChange_ + 1).arg(hunks_.size()));
    revert_->setEnabled(model_ && !model_->isReadOnly() && !hunks_.isEmpty() &&
                        modified_->text() == comparedModified_);
}
void DiffEditorWidget::setInlineMode(bool enabled) {
    if (!inline_ || inlineMode_ == enabled)
        return;
    inlineMode_ = enabled;
    splitter_->setVisible(!enabled);
    inline_->setVisible(enabled);
    active_ = enabled ? inline_ : modified_;
    decorate();
    active_->setFocus();
}
void DiffEditorWidget::decorateInline() {
    if (!hunks_.isEmpty() && modified_->text() != comparedModified_)
        return;
    const auto display = editor::makeInlineDiff(original_->text(), modified_->text(), hunks_, 3);
    const auto oldScroll = scroll_->value();
    synchronizing_ = true;
    inline_->setText(display.text);
    inline_->setInlineDiffDecorations(display.labels, display.removed, display.added,
                                      display.removedSpans, display.addedSpans);
    inlineChangeRows_ = display.changeRows;
    totalRows_ = display.labels.size();
    const int page = qMax(1, inline_->height() / qMax(1, inline_->diffLineHeight()));
    scroll_->setPageStep(page);
    scroll_->setRange(0, qMax(0, totalRows_ - page));
    synchronizing_ = false;
    scrollTo(oldScroll);
    counter_->setText(hunks_.isEmpty() ? tr("No changes") : tr("%1 changes").arg(hunks_.size()));
}
void DiffEditorWidget::revertCurrentChange() {
    finishUndoGroup();
    if (!model_ || model_->isReadOnly() || currentChange_ < 0 || currentChange_ >= hunks_.size() ||
        model_->text() != comparedModified_)
        return;
    const auto& h = hunks_[currentChange_];
    const auto original = original_->text();
    const auto working = model_->text();
    const auto a = original.split('\n'), b = working.split('\n');
    auto offset = [](const auto& lines, int line) {
        qint64 position = 0;
        for (int i = 0; i < line; ++i)
            position += lines[i].size() + 1;
        return position;
    };
    const auto startA = qMin(offset(a, h.originalStart), static_cast<qint64>(original.size()));
    const auto endA =
        qMin(offset(a, h.originalStart + h.originalCount), static_cast<qint64>(original.size()));
    const auto startB = qMin(offset(b, h.modifiedStart), static_cast<qint64>(working.size()));
    const auto endB =
        qMin(offset(b, h.modifiedStart + h.modifiedCount), static_cast<qint64>(working.size()));
    model_->replaceBytes(startB, endB - startB, original.mid(startA, endA - startA));
}
void DiffEditorWidget::showFind(bool replace) {
    if (!findBar_)
        return;
    findBar_->show();
    if (replace && !inlineMode_)
        active_ = modified_;
    replaceInput_->setVisible(replace && !inlineMode_ && model_ && !model_->isReadOnly());
    findBar_->findChild<QToolButton*>("diffReplaceNext")->setVisible(replaceInput_->isVisible());
    findInput_->setFocus();
    findInput_->selectAll();
}
void DiffEditorWidget::scrollTo(int row) {
    if (!original_ || synchronizing_)
        return;
    synchronizing_ = true;
    if (inlineMode_) {
        inline_->setDisplayLine(row);
        synchronizing_ = false;
        return;
    }
    originalGap_->setFixedHeight(qMax(0, originalTop_ - row) * original_->diffLineHeight());
    modifiedGap_->setFixedHeight(qMax(0, modifiedTop_ - row) * modified_->diffLineHeight());
    original_->setDisplayLine(qMax(0, row - originalTop_));
    modified_->setDisplayLine(qMax(0, row - modifiedTop_));
    synchronizing_ = false;
}
void DiffEditorWidget::navigate(int direction) {
    if (hunks_.isEmpty())
        return;
    currentChange_ = (currentChange_ + direction + static_cast<int>(hunks_.size())) %
                     static_cast<int>(hunks_.size());
    const auto& rows = inlineMode_ ? inlineChangeRows_ : changeRows_;
    if (currentChange_ < rows.size())
        scroll_->setValue(qMax(0, rows[currentChange_] - 3));
    counter_->setText(tr("%1 of %2 changes").arg(currentChange_ + 1).arg(hunks_.size()));
}
void DiffEditorWidget::undo() {
    finishUndoGroup();
    if (model_)
        model_->undo();
}
void DiffEditorWidget::redo() {
    finishUndoGroup();
    if (model_)
        model_->redo();
}
QString DiffEditorWidget::selectedText() const {
    if (!original_)
        return {};
    if (inlineMode_)
        return inline_->selectedText();
    return original_->isAncestorOf(QApplication::focusWidget()) ? original_->selectedText()
                                                                : modified_->selectedText();
}
bool DiffEditorWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusIn) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (original_ && (widget == original_ || original_->isAncestorOf(widget)))
                active_ = original_;
            if (modified_ && (widget == modified_ || modified_->isAncestorOf(widget)))
                active_ = modified_;
        }
    }
    if (event->type() == QEvent::Wheel && scroll_) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (!(wheel->modifiers() & Qt::ControlModifier) && wheel->angleDelta().y() != 0) {
            scroll_->setValue(scroll_->value() - wheel->angleDelta().y() / 40);
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && findBar_ && findBar_->isVisible()) {
            findBar_->hide();
            if (auto* editor = activeEditor())
                editor->setFocus();
            return true;
        }
        if (key->key() == Qt::Key_F7) {
            navigate(key->modifiers() & Qt::ShiftModifier ? -1 : 1);
            return true;
        }
        const auto* widget = qobject_cast<QWidget*>(watched);
        if (modified_ && widget && (widget == modified_ || modified_->isAncestorOf(widget))) {
            if (key->matches(QKeySequence::Undo)) {
                undo();
                return true;
            }
            if (key->matches(QKeySequence::Redo)) {
                redo();
                return true;
            }
            if (key->matches(QKeySequence::Save)) {
                emit saveRequested();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
void DiffEditorWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { decorate(); });
}
} // namespace litecode::ui
