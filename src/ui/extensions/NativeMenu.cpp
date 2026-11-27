#include "ui/extensions/NativeMenu.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"
#include "ui/components/PopupSurface.h"
#include <QAction>
#include <QMenu>
#include <QPalette>
#include <QStyle>
namespace litecode::ui {
namespace {
void stylePopover(components::Menu& menu) {
    menu.setProperty("uiMenuStyle", "popover");
    // Windows requires a frameless window for per-pixel transparency. Without
    // it, the unpainted area outside rounded corners becomes opaque black.
    menu.setWindowFlag(Qt::FramelessWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.style()->unpolish(&menu);
    menu.style()->polish(&menu);
    menu.setMinimumWidth(220);
}
void populate(components::Menu& menu, const QVector<extensions::NativeControl>& items,
              const std::function<void(const QString&)>& activated) {
    using extensions::NativeControlKind;
    for (const auto& item : items) {
        if (!item.visible)
            continue;
        if (item.kind == NativeControlKind::Separator) {
            menu.addSeparator();
            continue;
        }
        QAction* action;
        if (!item.children.isEmpty()) {
            auto* child = new components::Menu(item.text, &menu);
            stylePopover(*child);
            populate(*child, item.children, activated);
            action = menu.addMenu(child);
        } else {
            action = menu.addAction(item.text);
            if (item.kind == NativeControlKind::Button)
                QObject::connect(action, &QAction::triggered, &menu,
                                 [activated, id = item.id] { activated(id); });
        }
        action->setObjectName(item.id);
        action->setEnabled(item.enabled && item.kind == NativeControlKind::Button);
        if (!item.iconPath.isEmpty()) {
            action->setProperty("uiIconSource", item.iconPath);
            const auto color = menu.palette().color(QPalette::Text);
            action->setIcon(tintedIcon(item.iconPath, color, color));
        }
    }
}
} // namespace
void showNativeMenu(QMenu& menu, QWidget& anchor, bool alignRight) {
    bool preferAbove = false;
    for (auto* parent = anchor.parentWidget(); parent; parent = parent->parentWidget())
        if (parent->property("nativeAppearance") == "composer") {
            preferAbove = true;
            break;
        }
    menu.ensurePolished();
    const auto bounds =
        PopupSurface::anchoredGeometry(anchor, menu.sizeHint().expandedTo(menu.minimumSize()), true,
                                       preferAbove, alignRight ? Qt::AlignRight : Qt::AlignLeft);
    if (!bounds.isValid()) {
        menu.deleteLater();
        return;
    }
    menu.setMinimumSize(menu.minimumSize().boundedTo(bounds.size()));
    menu.setMaximumSize(bounds.size());
    menu.popup(bounds.topLeft());
}
QMenu* createNativeMenu(const extensions::NativeControl& control, QWidget* parent,
                        std::function<void(const QString&)> activated) {
    auto* menu = new components::Menu(parent);
    menu->setObjectName(control.id + ".menu");
    stylePopover(*menu);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    populate(*menu, control.children, activated);
    return menu;
}
} // namespace litecode::ui
