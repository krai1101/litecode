#include "ui/components/WorkbenchTabs.h"
#include "ui/components/Controls.h"
#include "ui/components/IconLabelButton.h"
#include <QHBoxLayout>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QWheelEvent>

namespace litecode::ui::components {
namespace {
class MeasuredTabBar final : public QTabBar {
  public:
    int naturalWidth(int index) const { return tabSizeHint(index).width(); }
};
} // namespace
WorkbenchTabs::WorkbenchTabs(TabOverflow overflow, QWidget* parent, QTabBar* bar)
    : QWidget(parent), overflow_(overflow), bar_(bar ? bar : new MeasuredTabBar) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new QWidget(this);
    header_->setObjectName("tabStripHeader");
    headerLayout_ = new QHBoxLayout(header_);
    headerLayout_->setContentsMargins(0, 0, 0, 0);
    headerLayout_->setSpacing(0);
    bar_->setExpanding(false);
    bar_->setDrawBase(false);
    bar_->setUsesScrollButtons(false);
    bar_->setElideMode(Qt::ElideNone);
    bar_->installEventFilter(this);
    if (overflow_ == TabOverflow::Scroll) {
        scroll_ = new QScrollArea(header_);
        scroll_->setObjectName("tabStripViewport");
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setWidgetResizable(false);
        scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll_->setFocusPolicy(Qt::NoFocus);
        scroll_->horizontalScrollBar()->setObjectName("workbenchTabScroll");
        scroll_->horizontalScrollBar()->setSingleStep(40);
        scroll_->setWidget(bar_);
        scroll_->viewport()->installEventFilter(this);
        tabArea_ = scroll_;
    } else {
        tabArea_ = new QWidget(header_);
        tabArea_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* row = new QHBoxLayout(tabArea_);
        row->setContentsMargins(0, 0, 4, 0);
        row->setSpacing(0);
        row->addWidget(bar_);
        more_ = new IconLabelButton(tabArea_);
        more_->setObjectName("panelTabOverflow");
        more_->setIconLabel("$(ellipsis)");
        more_->setFixedSize(24, 24);
        more_->setToolTip(tr("More panels"));
        more_->setAccessibleName(more_->toolTip());
        row->addWidget(more_);
        row->addStretch(1);
        connect(more_, &QToolButton::clicked, this, [this] {
            Menu menu(this);
            for (int i = 0; i < count(); ++i) {
                if (bar_->isTabVisible(i))
                    continue;
                auto* action = menu.addAction(bar_->tabText(i));
                connect(action, &QAction::triggered, this, [this, i] { setCurrentIndex(i); });
            }
            menu.exec(more_->mapToGlobal(QPoint(0, more_->height())));
        });
    }
    tabArea_->installEventFilter(this);
    headerLayout_->addWidget(tabArea_, 1);
    layout->addWidget(header_);
    stack_ = new QStackedWidget(this);
    stack_->setObjectName("tabPages");
    layout->addWidget(stack_, 1);
    connect(bar_, &QTabBar::currentChanged, this, [this](int index) {
        if (auto* page = widget(index))
            stack_->setCurrentWidget(page);
        refreshTabs();
        revealCurrent();
        QTimer::singleShot(0, this, &WorkbenchTabs::revealCurrent);
        emit currentChanged(index);
    });
    connect(bar_, &QTabBar::tabCloseRequested, this, &WorkbenchTabs::tabCloseRequested);
    connect(bar_, &QTabBar::tabMoved, this, [this](int from, int to) {
        pages_.move(from, to);
        if (auto* page = currentWidget())
            stack_->setCurrentWidget(page);
        scheduleRefresh();
    });
}
QScrollBar* WorkbenchTabs::horizontalScrollBar() const {
    return scroll_ ? scroll_->horizontalScrollBar() : nullptr;
}
int WorkbenchTabs::addTab(QWidget* page, const QString& text) { return addTab(page, {}, text); }
int WorkbenchTabs::addTab(QWidget* page, const QIcon& icon, const QString& text) {
    pages_.append(page);
    stack_->addWidget(page);
    const int index = bar_->addTab(icon, text);
    refreshTabs();
    return index;
}
void WorkbenchTabs::removeTab(int index) {
    if (index < 0 || index >= count())
        return;
    auto* previous = currentWidget();
    const int previousIndex = currentIndex();
    stack_->removeWidget(pages_.takeAt(index));
    {
        const QSignalBlocker blocked(bar_);
        bar_->removeTab(index);
    }
    if (auto* page = currentWidget())
        stack_->setCurrentWidget(page);
    refreshTabs();
    revealCurrent();
    if (previous != currentWidget() || previousIndex != currentIndex())
        emit currentChanged(currentIndex());
}
QWidget* WorkbenchTabs::widget(int index) const { return pages_.value(index); }
QWidget* WorkbenchTabs::currentWidget() const { return widget(currentIndex()); }
int WorkbenchTabs::currentIndex() const { return bar_->currentIndex(); }
void WorkbenchTabs::setCurrentIndex(int index) {
    if (index >= 0 && index < count())
        bar_->setCurrentIndex(index);
}
void WorkbenchTabs::setTabText(int index, const QString& text) {
    bar_->setTabText(index, text);
    refreshTabs();
}
QString WorkbenchTabs::tabText(int index) const { return bar_->tabText(index); }
QIcon WorkbenchTabs::tabIcon(int index) const { return bar_->tabIcon(index); }
void WorkbenchTabs::setTabToolTip(int index, const QString& text) {
    bar_->setTabToolTip(index, text);
}
QString WorkbenchTabs::tabToolTip(int index) const { return bar_->tabToolTip(index); }
void WorkbenchTabs::setTabsClosable(bool value) { bar_->setTabsClosable(value); }
bool WorkbenchTabs::tabsClosable() const { return bar_->tabsClosable(); }
void WorkbenchTabs::setMovable(bool value) { bar_->setMovable(value); }
bool WorkbenchTabs::isMovable() const { return bar_->isMovable(); }
void WorkbenchTabs::setDocumentMode(bool value) { bar_->setDocumentMode(value); }
void WorkbenchTabs::setIconSize(const QSize& size) {
    bar_->setIconSize(size);
    refreshTabs();
}
void WorkbenchTabs::setCornerWidget(QWidget* widget, Qt::Corner) {
    if (corner_) {
        headerLayout_->removeWidget(corner_);
        corner_->hide();
    }
    corner_ = widget;
    if (corner_)
        headerLayout_->addWidget(corner_);
    scheduleRefresh();
}
void WorkbenchTabs::scheduleRefresh() {
    if (refreshPending_ || refreshing_)
        return;
    refreshPending_ = true;
    QTimer::singleShot(0, this, [this] {
        refreshPending_ = false;
        refreshTabs();
    });
}
void WorkbenchTabs::refreshTabs() {
    if (refreshing_)
        return;
    const QScopedValueRollback guard(refreshing_, true);
    if (scroll_) {
        const QSize hint = bar_->sizeHint();
        bar_->resize(hint.width(), hint.height());
        header_->setFixedHeight(hint.height() + 4);
    } else {
        const auto* measured = dynamic_cast<const MeasuredTabBar*>(bar_);
        const auto widthOf = [this, measured](int index) {
            return measured ? measured->naturalWidth(index)
                            : bar_->fontMetrics().horizontalAdvance(bar_->tabText(index)) + 26;
        };
        int totalWidth = 0;
        for (int i = 0; i < count(); ++i)
            totalWidth += widthOf(i);
        const bool overflow = totalWidth > qMax(0, tabArea_->width() - 4);
        const int available = qMax(0, tabArea_->width() - (overflow ? 28 : 4));
        const int selectedWidth = currentIndex() >= 0 ? widthOf(currentIndex()) : 0;
        int used = selectedWidth;
        for (int i = 0; i < count(); ++i) {
            const int width = widthOf(i);
            const bool fits = i == currentIndex() || used + width <= available;
            bar_->setTabVisible(i, fits);
            if (fits && i != currentIndex())
                used += width;
        }
        // Visibility changes invalidate QTabBar's size hint, but its enclosing
        // layout can retain the old width. Keep its geometry in sync so newly
        // restored tabs are actually painted and clickable after expansion.
        bar_->setFixedWidth(bar_->sizeHint().width());
        tabArea_->setMinimumWidth(selectedWidth + 28);
        more_->setVisible(overflow);
    }
}
void WorkbenchTabs::revealCurrent() {
    if (!scroll_ || currentIndex() < 0)
        return;
    const auto rect = bar_->tabRect(currentIndex());
    auto* horizontal = scroll_->horizontalScrollBar();
    if (rect.left() < horizontal->value())
        horizontal->setValue(rect.left());
    else if (rect.right() >= horizontal->value() + scroll_->viewport()->width())
        horizontal->setValue(rect.right() - scroll_->viewport()->width() + 1);
}
bool WorkbenchTabs::eventFilter(QObject* watched, QEvent* event) {
    if (scroll_ && event->type() == QEvent::Wheel &&
        (watched == bar_ || watched == scroll_->viewport())) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        auto pixels = wheel->pixelDelta();
        int delta = pixels.x() ? pixels.x() : pixels.y();
        if (!delta) {
            const auto angle = wheel->angleDelta();
            delta = (angle.x() ? angle.x() : angle.y()) / 3;
        }
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
        wheel->accept();
        return true;
    }
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::FontChange || event->type() == QEvent::Resize)
        scheduleRefresh();
    return QWidget::eventFilter(watched, event);
}
void WorkbenchTabs::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    scheduleRefresh();
}
} // namespace litecode::ui::components
