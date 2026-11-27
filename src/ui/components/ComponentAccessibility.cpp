#include "ui/components/ComponentAccessibility.h"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QAbstractButton>
#include <QWidget>
#include <QVariant>
#include <mutex>

namespace litecode::ui::components {
#if QT_CONFIG(accessibility)
namespace {
class AccessibleComponent final : public QAccessibleWidget {
  public:
    explicit AccessibleComponent(QWidget* widget) : QAccessibleWidget(widget, QAccessible::Button) {}
    QAccessible::Role role() const override {
        const auto value = widget()->property("accessibilityRole").toString();
        if (value == "link") return QAccessible::Link;
        if (value == "checkbox") return QAccessible::CheckBox;
        if (value == "radio") return QAccessible::RadioButton;
        if (value == "menuitem") return QAccessible::MenuItem;
        if (value == "status") return QAccessible::StatusBar;
        if (value == "text") return QAccessible::StaticText;
        return QAccessible::Button;
    }
    QStringList actionNames() const override {
        auto result = QAccessibleWidget::actionNames();
        if (widget()->property("statusBarInteractive").toBool() && widget()->isEnabled()) result.prepend(pressAction());
        return result;
    }
    void doAction(const QString& action) override {
        if (action == pressAction() && widget()->property("statusBarInteractive").toBool()) {
            if (auto* button = qobject_cast<QAbstractButton*>(widget()); button && button->isEnabled()) button->click();
        } else QAccessibleWidget::doAction(action);
    }
    QStringList keyBindingsForAction(const QString& action) const override {
        if (action == pressAction()) return {"Space", "Enter"};
        return QAccessibleWidget::keyBindingsForAction(action);
    }
};
QAccessibleInterface* factory(const QString&, QObject* object) {
    auto* widget = qobject_cast<QWidget*>(object);
    if (widget && widget->property("uiComponent") == "statusBarItem") return new AccessibleComponent(widget);
    return nullptr;
}
}
#endif
void installComponentAccessibility() {
#if QT_CONFIG(accessibility)
    static std::once_flag once;
    std::call_once(once, [] { QAccessible::installFactory(factory); });
#endif
}
}
