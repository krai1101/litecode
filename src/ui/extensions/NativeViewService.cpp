#include "ui/extensions/NativeViewService.h"
#include "ui/SidebarService.h"
#include "ui/ThemedIcon.h"
#include "ui/extensions/NativeViewWidget.h"
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSet>
#include <QVBoxLayout>

namespace litecode::ui {
NativeViewService::NativeViewService(extensions::WorkbenchNativeViews& views, QMainWindow& window,
                                     QToolBar* activityBar, QObject* parent, QMenu* viewMenu,
                                     SidebarService* sidebars)
    : QObject(parent), views_(views), window_(window),
      sidebars_(sidebars ? sidebars : new SidebarService(window, activityBar)),
      viewMenu_(viewMenu) {
    connect(&views_, &QObject::destroyed, this, &NativeViewService::detach);
    connect(sidebars_, &SidebarService::containerVisibilityChanged, this,
            [this](const QString& containerId, bool visible) {
                const auto membership = memberships_;
                for (auto it = membership.cbegin(); attached_ && it != membership.cend(); ++it)
                    if (it.value() == containerId)
                        views_.setVisible(it.key(), visible);
            });
    connect(sidebars_, &SidebarService::changed, this, &NativeViewService::layoutChanged);
    connect(&views_, &extensions::WorkbenchNativeViews::contributionsChanged, this,
            &NativeViewService::synchronize);
    connect(&views_, &extensions::WorkbenchNativeViews::contentChanged, this,
            [this](const QString& id, const extensions::NativeControl& root, quint64 revision) {
                if (widgets_.value(id))
                    widgets_[id]->setContent(root, revision);
            });
    connect(&views_, &extensions::WorkbenchNativeViews::textChanged, this,
            [this](const QString& id, const QString& control, const QString& text) {
                if (widgets_.value(id))
                    widgets_[id]->setText(control, text);
            });
    connect(&views_, &extensions::WorkbenchNativeViews::enabledChanged, this,
            [this](const QString& id, const QString& control, bool enabled) {
                if (widgets_.value(id))
                    widgets_[id]->setControlEnabled(control, enabled);
            });
    connect(&views_, &extensions::WorkbenchNativeViews::scrollToEndRequested, this,
            [this](const QString& id, const QString& control) {
                if (widgets_.value(id)) widgets_[id]->scrollToEnd(control);
            });
    connect(&views_, &extensions::WorkbenchNativeViews::unavailable, this,
            [this](const QString& id, const QString& error) {
                if (widgets_.value(id))
                    widgets_[id]->showError(error);
            });
    connect(&views_, &extensions::WorkbenchNativeViews::showRequested, this,
            [this](const QString& id, bool preserveFocus) {
                if (sidebars_)
                    sidebars_->open(memberships_.value(id), !preserveFocus);
            });
    synchronize();
}
NativeViewService::~NativeViewService() { detach(); }
void NativeViewService::setCodeFont(const QFont& font) {
    codeFont_ = font;
    for (const auto& widget : widgets_)
        if (widget)
            widget->setCodeFont(font);
}
void NativeViewService::openView(const QString& id) {
    if (attached_ && sidebars_)
        sidebars_->open(memberships_.value(id), true);
}
void NativeViewService::detach() {
    if (!attached_)
        return;
    attached_ = false;
    for (auto it = containers_.cbegin(); it != containers_.cend(); ++it) {
        if (sidebars_)
            sidebars_->removeContainer(it.key());
        if (viewMenu_ && it->action)
            viewMenu_->removeAction(it->action);
        if (it->action)
            it->action->deleteLater();
        if (it->body) {
            it->body->hide();
            it->body->deleteLater();
        }
    }
    containers_.clear();
    widgets_.clear();
    memberships_.clear();
}
void NativeViewService::synchronize() {
    if (!attached_ || !sidebars_)
        return;
    const auto contributions = views_.contributions();
    QHash<QString, int> counts;
    for (const auto& c : contributions)
        ++counts[c.owner + '/' + c.container];
    QSet<QString> retainedViews, retainedContainers;
    for (const auto& c : contributions) {
        const auto containerId = c.owner + '/' + c.container;
        retainedViews.insert(c.id);
        retainedContainers.insert(containerId);
        if (!containers_.contains(containerId)) {
            auto* body = new QWidget;
            body->setObjectName("extensionContainer:" + containerId);
            auto* layout = new QVBoxLayout(body);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            auto* action = new QAction(c.containerTitle, body);
            action->setObjectName("extensionContainerAction:" + containerId);
            const auto icon =
                QFileInfo(QDir(c.extensionRoot).filePath(c.iconPath)).canonicalFilePath();
            const auto root = QFileInfo(c.extensionRoot).canonicalFilePath();
            const auto relative = QDir(root).relativeFilePath(icon);
            if (!icon.isEmpty() && !root.isEmpty() && !QDir::isAbsolutePath(relative) &&
                relative != ".." && !relative.startsWith("../"))
                setThemedIcon(action, icon);
            else
                setThemedIcon(action, ":/icons/extensions.svg");
            if (viewMenu_)
                viewMenu_->addAction(action);
            containers_.insert(containerId, {body, action, layout});
            sidebars_->addContainer(containerId, c.containerTitle, body, action,
                                    c.location == "secondarySidebar"
                                        ? SidebarService::Location::Secondary
                                        : SidebarService::Location::Primary);
        }
        if (!widgets_.contains(c.id)) {
            auto& container = containers_[containerId];
            auto* label = new QLabel(c.title, container.body);
            label->setObjectName("extensionViewTitle:" + c.id);
            label->setTextFormat(Qt::PlainText);
            label->setMargin(8);
            label->setVisible(counts.value(containerId) > 1 || c.title != c.containerTitle);
            auto* widget = new NativeViewWidget(container.body);
            if (codeFont_)
                widget->setCodeFont(*codeFont_);
            widget->setObjectName("extensionView:" + c.id);
            container.layout->addWidget(label);
            container.layout->addWidget(widget, 1);
            widgets_.insert(c.id, widget);
            memberships_.insert(c.id, containerId);
            connect(widget, &NativeViewWidget::input, this,
                    [this, id = c.id](quint64 revision, const extensions::NativeViewEvent& event) {
                        if (attached_)
                            views_.input(id, revision, event);
                    });
            if (sidebars_->containerVisible(containerId))
                views_.setVisible(c.id, true);
        }
    }
    for (const auto& id : widgets_.keys())
        if (!retainedViews.contains(id)) {
            const auto widget = widgets_.take(id);
            memberships_.remove(id);
            if (auto* label = window_.findChild<QLabel*>("extensionViewTitle:" + id))
                label->deleteLater();
            if (widget) {
                widget->hide();
                widget->deleteLater();
            }
        }
    for (const auto& id : containers_.keys())
        if (!retainedContainers.contains(id)) {
            const auto container = containers_.take(id);
            sidebars_->removeContainer(id);
            if (viewMenu_ && container.action)
                viewMenu_->removeAction(container.action);
            if (container.action)
                container.action->deleteLater();
            if (container.body) {
                container.body->hide();
                container.body->deleteLater();
            }
        }
}
} // namespace litecode::ui
