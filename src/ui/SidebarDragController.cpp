#include "ui/SidebarDragController.h"
#include "ui/SidebarService.h"
#include "ui/Theme.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QUuid>

namespace litecode::ui {
namespace {
constexpr auto containerMime = "application/x-litecode-sidebar-container";
class SidebarDropPreview final : public QWidget {
  public:
    explicit SidebarDropPreview(QWidget* parent) : QWidget(parent) {
        setObjectName("sidebarDropPreview");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        const auto tokens = Theme::tokens(qApp->property("litecodeDarkTheme").toBool());
        QPainter painter(this);
        auto fill = tokens.focus;
        fill.setAlpha(35);
        painter.fillRect(rect(), fill);
        painter.setPen(QPen(tokens.focus, 2));
        painter.drawRect(rect().adjusted(1, 1, -1, -1));
    }
};
SidebarService::Location place(int destination) {
    return destination == 0 ? SidebarService::Location::Primary
                            : SidebarService::Location::Secondary;
}
} // namespace
SidebarDragController::SidebarDragController(SidebarService& sidebars, QMainWindow& window,
                                             QToolBar* activityBar, QObject* parent)
    : QObject(parent), sidebars_(sidebars), window_(window), activityBar_(activityBar),
      preview_(new SidebarDropPreview(&window)),
      instance_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    window.setAcceptDrops(true);
    qApp->installEventFilter(this);
}
SidebarDragController::~SidebarDragController() {
    shutdown();
    delete preview_.data();
}
void SidebarDragController::shutdown() {
    if (stopped_)
        return;
    stopped_ = true;
    qApp->removeEventFilter(this);
    clearPreview();
    if (dragging_)
        QDrag::cancel();
}
std::unique_ptr<QMimeData> SidebarDragController::createMimeData(const QString& container) const {
    if (stopped_ || !sidebars_.canMove(container))
        return {};
    auto mime = std::make_unique<QMimeData>();
    mime->setData(containerMime,
                  QJsonDocument(QJsonObject{{"window", instance_}, {"container", container}})
                      .toJson(QJsonDocument::Compact));
    return mime;
}
QString SidebarDragController::readContainer(const QMimeData* mime) const {
    if (!mime || !mime->hasFormat(containerMime))
        return {};
    const auto data = mime->data(containerMime);
    if (data.size() > 4096)
        return {};
    const auto root = QJsonDocument::fromJson(data).object();
    const auto id = root.value("container").toString();
    return root.value("window").toString() == instance_ && sidebars_.canMove(id) ? id : QString{};
}
QString SidebarDragController::sourceAt(QWidget* widget, const QPoint& position) const {
    if (activityBar_ && (widget == activityBar_ || activityBar_->isAncestorOf(widget))) {
        for (auto* action : activityBar_->actions()) {
            auto* button = activityBar_->widgetForAction(action);
            if (button && (widget == button || button->isAncestorOf(widget)))
                return action->property("sidebarContainerId").toString();
        }
    }
    for (auto location : {SidebarService::Location::Primary, SidebarService::Location::Secondary}) {
        auto* header = sidebars_.dock(location)->titleBarWidget();
        if (widget != header && !header->isAncestorOf(widget))
            continue;
        if (auto* tabs = qobject_cast<QTabBar*>(widget)) {
            const int index = tabs->tabAt(tabs->mapFromGlobal(position));
            return index < 0 ? QString{} : tabs->tabData(index).toString();
        }
        // Header action buttons remain buttons, never drag handles.
        for (auto* ancestor = widget; ancestor && ancestor != header;
             ancestor = ancestor->parentWidget())
            if (qobject_cast<QAbstractButton*>(ancestor))
                return {};
        return sidebars_.activeContainer(location);
    }
    return {};
}
int SidebarDragController::destinationAt(const QPoint& globalPosition) const {
    const auto position = window_.mapFromGlobal(globalPosition);
    if (!window_.rect().contains(position))
        return -1;
    const int top = window_.menuWidget() ? window_.menuWidget()->geometry().bottom() + 1 : 0;
    const auto* status = window_.findChild<QStatusBar*>();
    const int bottom = status && status->isVisible() ? status->geometry().top() : window_.height();
    if (position.y() < top || position.y() >= bottom)
        return -1;
    for (int destination : {0, 1}) {
        auto* dock = sidebars_.dock(place(destination));
        if (!dock->isHidden() && dock->geometry().contains(position))
            return destination;
    }
    // A closed/empty side bar still has an editor-edge drop target. Preview it
    // without opening a real host or changing persisted visibility on drag-over.
    const int extent = qBound(48, window_.width() / 6, 180);
    if (position.x() >= window_.width() - extent)
        return 1;
    const int activityWidth = activityBar_ && activityBar_->isVisible() ? activityBar_->width() : 0;
    if (position.x() <= activityWidth + extent)
        return 0;
    return -1;
}
void SidebarDragController::showPreview(int destination) {
    if (!preview_)
        return;
    auto* dock = sidebars_.dock(place(destination));
    QRect area = dock->geometry();
    if (dock->isHidden()) {
        const int top = window_.menuWidget() ? window_.menuWidget()->geometry().bottom() + 1 : 0;
        const auto* status = window_.findChild<QStatusBar*>();
        const int bottom =
            status && status->isVisible() ? status->geometry().top() : window_.height();
        const int width = qBound(170, window_.width() / 4, 360);
        const int activityWidth =
            activityBar_ && activityBar_->isVisible() ? activityBar_->width() : 0;
        area = QRect(destination == 1 ? window_.width() - width : activityWidth, top, width,
                     bottom - top);
    }
    preview_->setAccessibleName(destination == 1 ? tr("Move to Secondary Side Bar")
                                                 : tr("Move to Primary Side Bar"));
    preview_->setGeometry(area);
    preview_->show();
    preview_->raise();
    preview_->update();
    previewDestination_ = destination;
}
void SidebarDragController::clearPreview() {
    if (preview_)
        preview_->hide();
    previewDestination_ = -1;
}
void SidebarDragController::startDrag(const QString& container, QWidget* source) {
    auto mime = createMimeData(container);
    if (!mime)
        return;
    if (auto* button = qobject_cast<QAbstractButton*>(source))
        button->setDown(false);
    // The workbench is a stable drag owner. Moving an activity entry may delete
    // its old toolbar button while the native drag loop is still unwinding.
    QDrag drag(&window_);
    drag.setMimeData(mime.release());
    if (source)
        drag.setPixmap(source->grab());
    dragging_ = true;
    drag.exec(Qt::MoveAction, Qt::MoveAction);
    dragging_ = false;
    clearPreview();
}
bool SidebarDragController::eventFilter(QObject* watched, QEvent* event) {
    if (stopped_)
        return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || widget->window() != &window_)
        return false;
    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !dragging_) {
            pressContainer_ = sourceAt(widget, mouse->globalPosition().toPoint());
            pressSource_ = pressContainer_.isEmpty() ? nullptr : widget;
            pressPosition_ = mouse->globalPosition().toPoint();
            const bool activity =
                activityBar_ && (widget == activityBar_ || activityBar_->isAncestorOf(widget));
            if (pressSource_ && !activity && !qobject_cast<QTabBar*>(widget))
                return true; // A title/blank-header press must not propagate and clear its drag
                             // source.
        }
    } else if (event->type() == QEvent::MouseMove && pressSource_ && !dragging_) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (!(mouse->buttons() & Qt::LeftButton)) {
            pressSource_ = nullptr;
            pressContainer_.clear();
        } else if ((mouse->globalPosition().toPoint() - pressPosition_).manhattanLength() >=
                   QApplication::startDragDistance()) {
            const auto id = pressContainer_;
            const auto source = pressSource_;
            pressSource_ = nullptr;
            pressContainer_.clear();
            startDrag(id, source);
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        pressSource_ = nullptr;
        pressContainer_.clear();
        if (dragging_)
            return true;
    }
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
        event->type() == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        const auto id = readContainer(drop->mimeData());
        if (id.isEmpty())
            return false;
        const int destination = destinationAt(widget->mapToGlobal(drop->position().toPoint()));
        if (destination < 0 || !(drop->possibleActions() & Qt::MoveAction)) {
            clearPreview();
            drop->ignore();
            return true;
        }
        drop->setDropAction(Qt::MoveAction);
        drop->accept();
        if (event->type() == QEvent::Drop) {
            clearPreview();
            sidebars_.move(id, place(destination));
            sidebars_.open(id, true);
        } else
            showPreview(destination);
        return true;
    }
    if (event->type() == QEvent::DragLeave)
        clearPreview();
    return false;
}
} // namespace litecode::ui
