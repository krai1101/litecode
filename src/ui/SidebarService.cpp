#include "ui/SidebarService.h"
#include "ui/SidebarDragController.h"
#include "ui/WorkbenchChrome.h"
#include "ui/WorkbenchDock.h"
#include <QAction>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace litecode::ui {
SidebarService::Host& SidebarService::host(Location location) {
    return hosts_[static_cast<size_t>(location)];
}
const SidebarService::Host& SidebarService::host(Location location) const {
    return hosts_[static_cast<size_t>(location)];
}
SidebarService::SidebarService(QMainWindow& window, QToolBar* activityBar, QDockWidget* primary)
    : QObject(&window), window_(window), activityBar_(activityBar), parking_(new QWidget(&window)) {
    parking_->hide();
    for (auto location : {Location::Primary, Location::Secondary}) {
        auto& part = host(location);
        part.dock = location == Location::Primary && primary ? primary
                                                             : new WorkbenchDock({}, true, &window);
        part.dock->setObjectName(location == Location::Primary ? "workspaceDock"
                                                               : "secondarySidebarDock");
        part.dock->setFeatures(QDockWidget::DockWidgetClosable);
        part.dock->setMinimumWidth(170);
        const auto area =
            location == Location::Primary ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
        part.dock->setAllowedAreas(area);
        if (!part.dock->titleBarWidget())
            part.dock->setTitleBarWidget(createWorkbenchDockHeader(
                part.dock, {}, nullptr, location == Location::Secondary, true));
        if (auto* old = part.dock->widget()) {
            old->hide();
            old->setParent(parking_);
        }
        part.pages = new QStackedWidget(part.dock);
        part.pages->setObjectName("sidebarContainers");
        // The host owns sidebar width. Inactive views' size hints must not
        // enlarge the shared dock's 170px minimum.
        part.pages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        part.dock->setWidget(part.pages);
        part.actions = new QWidget(part.dock->titleBarWidget());
        auto* actionsLayout = new QHBoxLayout(part.actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(2);
        auto* headerLayout = qobject_cast<QHBoxLayout*>(part.dock->titleBarWidget()->layout());
        auto* options =
            part.dock->titleBarWidget()->findChild<QToolButton*>("workbenchViewActions");
        headerLayout->insertWidget(options ? headerLayout->indexOf(options) : headerLayout->count(),
                                   part.actions);
        if (location == Location::Secondary) {
            part.empty = new QWidget(part.pages);
            part.empty->setObjectName("secondarySidebarEmpty");
            auto* emptyLayout = new QVBoxLayout(part.empty);
            emptyLayout->setContentsMargins(16, 16, 16, 16);
            auto* message = new QLabel(tr("No views are available here. Enable an extension in "
                                          "Extensions to open its view."),
                                       part.empty);
            message->setWordWrap(true);
            emptyLayout->addWidget(message);
            emptyLayout->addStretch();
            part.pages->addWidget(part.empty);
            part.tabs = new QTabBar(part.dock->titleBarWidget());
            part.tabs->setObjectName("sidebarTabs");
            part.tabs->setAccessibleName(tr("Secondary Side Bar views"));
            part.tabs->setExpanding(false);
            part.tabs->setDrawBase(false);
            part.tabs->setElideMode(Qt::ElideRight);
            headerLayout->insertWidget(0, part.tabs, 1);
            if (auto* title = part.dock->titleBarWidget()->findChild<QLabel*>("dockHeaderTitle"))
                title->hide();
            connect(part.tabs, &QTabBar::currentChanged, this, [this](int index) {
                if (!updating_ && index >= 0)
                    open(host(Location::Secondary).tabs->tabData(index).toString());
            });
        }
        window.addDockWidget(area, part.dock);
        part.dock->hide();
        part.dock->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(part.dock, &QWidget::customContextMenuRequested, this,
                [this, location](const QPoint& position) { showContextMenu(location, position); });
        // The shared header's overflow uses these host-level actions, dispatching
        // against the active container rather than owning extension-specific logic.
        for (auto destination : {Location::Primary, Location::Secondary}) {
            auto* action =
                new QAction(destination == Location::Primary ? tr("Move to Primary Side Bar")
                                                             : tr("Move to Secondary Side Bar"),
                            part.dock);
            part.dock->addAction(action);
            connect(action, &QAction::triggered, this,
                    [this, location, destination] { move(host(location).active, destination); });
        }
        connect(part.dock, &QDockWidget::visibilityChanged, this, [this, location](bool visible) {
            if (updating_ || (!visible && !host(location).dock->isHidden()))
                return; // Minimizing the window is not closing the sidebar.
            host(location).desiredVisible = visible;
            refresh();
            emit changed();
        });
    }
    dragController_ = new SidebarDragController(*this, window_, activityBar_, this);
}
void SidebarService::addContainer(const QString& id, const QString& title, QWidget* content,
                                  QAction* action, Location location, QWidget* titleActions,
                                  bool bindAction, bool available) {
    if (stopped_ || id.isEmpty() || !content || entries_.contains(id))
        return;
    if (savedLocations_.contains(id))
        location = savedLocations_.value(id).toString() == "secondary" ? Location::Secondary
                                                                       : Location::Primary;
    const bool openDefault = location == Location::Secondary && available &&
                             host(location).active.isEmpty() &&
                             host(location).desiredActive.isEmpty();
    Entry entry{title, content, titleActions, action, location, available, false, false, {}};
    if (action) {
        action->setProperty("sidebarContainerId", id);
        action->setCheckable(true);
        if (bindAction)
            entry.connections.append(connect(action, &QAction::triggered, this, [this, id] {
                const auto place = entries_.value(id).location;
                if (host(place).active == id && !host(place).dock->isHidden())
                    setVisible(place, false);
                else
                    open(id, true);
            }));
    }
    entries_.insert(id, entry);
    order_.append(id);
    host(location).pages->addWidget(content);
    if (titleActions) {
        titleActions->hide();
        titleActions->setParent(parking_);
    }
    if (host(location).active.isEmpty())
        host(location).active = id;
    if (host(location).desiredActive == id)
        host(location).active = id;
    if (openDefault) {
        host(location).desiredActive = id;
        host(location).desiredVisible = true;
    }
    refresh();
    if (host(location).desiredActive == id && host(location).desiredVisible)
        setVisible(location, true);
    if (legacyRestoreEnabled_)
        migrateLegacyContainer(id);
}
bool SidebarService::contains(const QString& id) const { return entries_.contains(id); }
bool SidebarService::isAvailable(const QString& id) const {
    const auto it = entries_.constFind(id);
    return !stopped_ && it != entries_.cend() && it->available && it->content;
}
bool SidebarService::canMove(const QString& id) const {
    return isAvailable(id) && !entries_.value(id).fixed;
}
void SidebarService::setFixedLocation(const QString& id, Location location) {
    auto it = entries_.find(id);
    if (it == entries_.end())
        return;
    if (it->location != location) {
        const auto source = it->location;
        host(source).pages->removeWidget(it->content);
        it->location = location;
        host(location).pages->addWidget(it->content);
        selectFallback(source);
        host(source).desiredActive = host(source).active;
    }
    it->fixed = true;
    savedLocations_.insert(id, location == Location::Primary ? "primary" : "secondary");
    refresh();
}
SidebarService::Location SidebarService::location(const QString& id) const {
    return entries_.value(id).location;
}
QDockWidget* SidebarService::dock(Location location) const { return host(location).dock; }
QString SidebarService::activeContainer(Location location) const { return host(location).active; }
bool SidebarService::containerVisible(const QString& id) const {
    auto it = entries_.constFind(id);
    return it != entries_.cend() && it->available && host(it->location).active == id &&
           !host(it->location).dock->isHidden();
}
void SidebarService::open(const QString& id, bool focus) {
    if (stopped_)
        return;
    const auto it = entries_.constFind(id);
    if (it == entries_.cend() || !it->available || !it->content)
        return;
    const auto place = it->location;
    host(place).active = id;
    host(place).desiredActive = id;
    host(place).desiredVisible = true;
    refresh();
    host(place).dock->show();
    restoreCollapsedWidth(place);
    host(place).dock->raise();
    refresh();
    if (!entries_.contains(id) || host(place).active != id)
        return; // Activation may have synchronously disposed/replaced the container.
    if (focus && entries_.value(id).content)
        entries_.value(id).content->setFocus(Qt::OtherFocusReason);
    emit activeContainerChanged(id);
    emit changed();
}
void SidebarService::selectFallback(Location place) {
    auto& part = host(place);
    const auto current = entries_.constFind(part.active);
    if (current != entries_.cend() && current->location == place && current->available)
        return;
    part.active.clear();
    for (const auto& id : order_)
        if (entries_[id].location == place && entries_[id].available) {
            part.active = id;
            break;
        }
    if (part.active.isEmpty() && part.empty) {
        part.pages->setCurrentWidget(part.empty);
        part.dock->setWindowTitle(tr("Secondary Side Bar"));
    } else if (part.active.isEmpty()) {
        const QSignalBlocker blocker(part.dock);
        part.dock->hide();
    }
}
void SidebarService::move(const QString& id, Location destination) {
    if (stopped_)
        return;
    auto it = entries_.find(id);
    if (it == entries_.end() || it->fixed || it->location == destination)
        return;
    const auto source = it->location;
    host(source).pages->removeWidget(it->content);
    it->location = destination;
    savedLocations_.insert(id, destination == Location::Primary ? "primary" : "secondary");
    host(destination).pages->addWidget(it->content);
    selectFallback(source);
    host(source).desiredActive = host(source).active;
    open(id);
}
void SidebarService::setAvailable(const QString& id, bool available) {
    if (stopped_)
        return;
    auto it = entries_.find(id);
    if (it == entries_.end() || it->available == available)
        return;
    const auto place = it->location;
    it->available = available;
    selectFallback(place);
    refresh();
    emit changed();
}
void SidebarService::removeContainer(const QString& id) {
    if (stopped_)
        return;
    auto it = entries_.find(id);
    if (it == entries_.end())
        return;
    const auto place = it->location;
    const bool wasVisible = it->reportedVisible;
    for (const auto& connection : it->connections)
        disconnect(connection);
    if (activityBar_ && it->action)
        activityBar_->removeAction(it->action);
    if (it->action)
        it->action->setProperty("sidebarContainerId", QVariant{});
    if (it->content) {
        host(place).pages->removeWidget(it->content);
        it->content->hide();
        it->content->setParent(parking_);
    }
    if (it->titleActions) {
        it->titleActions->hide();
        it->titleActions->setParent(parking_);
    }
    entries_.erase(it);
    order_.removeAll(id);
    selectFallback(place);
    refresh();
    if (wasVisible)
        emit containerVisibilityChanged(id, false);
    emit changed();
}
void SidebarService::setVisible(Location place, bool visible) {
    if (stopped_)
        return;
    auto& part = host(place);
    part.desiredVisible = visible;
    selectFallback(place);
    part.dock->setVisible(visible && (!part.active.isEmpty() || part.empty));
    if (visible)
        restoreCollapsedWidth(place);
    refresh();
    emit changed();
}
void SidebarService::toggle(Location place) { setVisible(place, host(place).dock->isHidden()); }
void SidebarService::refresh() {
    if (updating_ || stopped_)
        return;
    QList<QPair<QString, bool>> notifications;
    {
        QScopedValueRollback guard(updating_, true);
        for (auto place : {Location::Primary, Location::Secondary}) {
            auto& part = host(place);
            selectFallback(place);
            if (part.tabs) {
                while (part.tabs->count())
                    part.tabs->removeTab(0);
                for (const auto& id : order_)
                    if (entries_[id].location == place && entries_[id].available) {
                        const auto& entry = entries_[id];
                        const int index = part.tabs->addTab(entry.title);
                        part.tabs->setTabData(index, id);
                        if (id == part.active)
                            part.tabs->setCurrentIndex(index);
                    }
                part.tabs->setVisible(part.tabs->count() > 0);
                if (auto* title =
                        part.dock->titleBarWidget()->findChild<QLabel*>("dockHeaderTitle"))
                    title->setVisible(part.active.isEmpty());
            }
            if (entries_.contains(part.active)) {
                const auto& entry = entries_[part.active];
                part.pages->setCurrentWidget(entry.content);
                part.dock->setWindowTitle(entry.title);
            }
            for (auto* action : part.dock->actions())
                action->setVisible(canMove(part.active));
            if (auto* options =
                    part.dock->titleBarWidget()->findChild<QToolButton*>("workbenchViewActions"))
                options->setVisible(canMove(part.active));
        }
        for (const auto& id : order_) {
            auto& entry = entries_[id];
            const bool selected = host(entry.location).active == id;
            const bool visible = containerVisible(id);
            if (entry.action) {
                const QSignalBlocker block(entry.action);
                entry.action->setChecked(visible);
                entry.action->setEnabled(entry.available);
                if (activityBar_) {
                    const bool inBar = activityBar_->actions().contains(entry.action);
                    if (entry.available && entry.location == Location::Primary && !inBar) {
                        QAction* before = activityBar_->findChild<QAction*>("activityBarSpacer");
                        if (entry.fixed) {
                            const int rank = entry.action->property("sidebarCoreOrder").toInt();
                            for (auto* candidate : activityBar_->actions()) {
                                const auto order = candidate->property("sidebarCoreOrder");
                                if ((order.isValid() && order.toInt() > rank) ||
                                    (!order.isValid() &&
                                     candidate->property("sidebarContainerId").isValid())) {
                                    before = candidate;
                                    break;
                                }
                            }
                        }
                        activityBar_->insertAction(before, entry.action);
                    } else if ((!entry.available || entry.location != Location::Primary) && inBar)
                        activityBar_->removeAction(entry.action);
                }
            }
            if (entry.titleActions) {
                entry.titleActions->hide();
                if (selected) {
                    host(entry.location).actions->layout()->addWidget(entry.titleActions);
                    const auto available =
                        entry.titleActions->property("sidebarTitleActionsAvailable");
                    entry.titleActions->setVisible(!available.isValid() || available.toBool());
                }
            }
            if (entry.reportedVisible != visible) {
                entry.reportedVisible = visible;
                notifications.append({id, visible});
            }
        }
        // Resolve/activation may synchronously register or remove contributions.
        // Publish only after iterating the registry and releasing the update guard.
    }
    for (const auto& notification : notifications)
        if (entries_.contains(notification.first) &&
            entries_[notification.first].reportedVisible == notification.second)
            emit containerVisibilityChanged(notification.first, notification.second);
}
QByteArray SidebarService::saveState() const {
    QJsonObject root{{"version", 1}, {"locations", savedLocations_}};
    for (auto place : {Location::Primary, Location::Secondary}) {
        const auto& part = host(place);
        root.insert(
            place == Location::Primary ? "primary" : "secondary",
            QJsonObject{{"active", part.desiredActive.isEmpty() ? part.active : part.desiredActive},
                        {"visible", part.desiredVisible},
                        {"collapsedWidth", part.collapsedWidth}});
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
void SidebarService::restoreState(const QByteArray& state) {
    const auto root = QJsonDocument::fromJson(state).object();
    if (root.value("version").toInt() != 1)
        return;
    savedLocations_ = root.value("locations").toObject();
    for (const auto& id : order_) {
        if (!savedLocations_.contains(id))
            continue;
        auto& entry = entries_[id];
        if (entry.fixed) {
            savedLocations_.insert(id,
                                   entry.location == Location::Primary ? "primary" : "secondary");
            continue;
        }
        const auto destination = savedLocations_.value(id).toString() == "secondary"
                                     ? Location::Secondary
                                     : Location::Primary;
        if (entry.fixed || entry.location == destination)
            continue;
        host(entry.location).pages->removeWidget(entry.content);
        entry.location = destination;
        host(destination).pages->addWidget(entry.content);
    }
    for (auto place : {Location::Primary, Location::Secondary}) {
        auto& part = host(place);
        const auto saved =
            root.value(place == Location::Primary ? "primary" : "secondary").toObject();
        part.desiredActive = saved.value("active").toString();
        part.desiredVisible = saved.value("visible").toBool();
        part.collapsedWidth = qMax(0, saved.value("collapsedWidth").toInt());
        if (entries_.contains(part.desiredActive))
            part.active = part.desiredActive;
    }
}
void SidebarService::migrateLegacyDockState() {
    legacyRestoreEnabled_ = true;
    for (const auto& id : order_)
        migrateLegacyContainer(id);
}
void SidebarService::migrateLegacyContainer(const QString& id) {
    if (id.startsWith("workbench.") || savedLocations_.contains(id))
        return;
    // Consume only this container's old Qt placeholder. No old extension dock
    // remains in the live layout and unrelated editor/panel geometry is kept.
    QDockWidget legacy(&window_);
    legacy.setObjectName("extensionContainer:" + id);
    if (!window_.restoreDockWidget(&legacy))
        return;
    const auto destination = window_.dockWidgetArea(&legacy) == Qt::RightDockWidgetArea
                                 ? Location::Secondary
                                 : Location::Primary;
    const bool visible = !legacy.isHidden();
    const int width = legacy.width();
    window_.removeDockWidget(&legacy);
    if (entries_[id].location != destination) {
        const auto previous = entries_[id].location;
        host(previous).pages->removeWidget(entries_[id].content);
        entries_[id].location = destination;
        host(destination).pages->addWidget(entries_[id].content);
        selectFallback(previous);
    }
    savedLocations_.insert(id, destination == Location::Secondary ? "secondary" : "primary");
    if (visible) {
        open(id);
        window_.resizeDocks({host(destination).dock}, {qMax(170, width)}, Qt::Horizontal);
    } else
        refresh();
}
void SidebarService::applyRestoredState() {
    for (auto place : {Location::Primary, Location::Secondary}) {
        auto& part = host(place);
        if (entries_.contains(part.desiredActive))
            part.active = part.desiredActive;
        setVisible(place, part.desiredVisible);
    }
}
void SidebarService::showContextMenu(Location place, const QPoint& position) {
    QMenu menu(host(place).dock);
    menu.addActions(host(place).dock->actions());
    menu.exec(host(place).dock->mapToGlobal(position));
}
void SidebarService::shutdown() {
    stopped_ = true;
    if (dragController_)
        dragController_->shutdown();
    for (auto& part : hosts_)
        disconnect(part.dock, nullptr, this, nullptr);
    disconnect(this, nullptr, nullptr, nullptr);
}
} // namespace litecode::ui
