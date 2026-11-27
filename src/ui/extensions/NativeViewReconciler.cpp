#include "ui/extensions/NativeViewReconciler.h"
#include <QLayout>
#include <QScrollArea>
namespace litecode::ui {
namespace {
using extensions::NativeControl;
using extensions::NativeControlKind;
bool sameTree(const NativeControl& a, const NativeControl& b) {
    if (a.kind == NativeControlKind::Dialog || a.id != b.id || a.kind != b.kind ||
        a.appearance != b.appearance || a.buttonStyle != b.buttonStyle || a.text != b.text ||
        a.iconPath != b.iconPath || a.iconSize != b.iconSize ||
        a.itemIconPaths != b.itemIconPaths || a.itemColors != b.itemColors ||
        a.helpText != b.helpText || a.helpUrl != b.helpUrl ||
        a.accessibleName != b.accessibleName || a.placeholder != b.placeholder ||
        a.toolTip != b.toolTip || a.items != b.items || a.enabled != b.enabled ||
        a.visible != b.visible || a.readOnly != b.readOnly || a.password != b.password || a.checked != b.checked ||
        a.spacing != b.spacing || a.margin != b.margin || a.contentsMargins != b.contentsMargins ||
        a.stretch != b.stretch || a.currentIndex != b.currentIndex || a.value != b.value ||
        a.children.size() != b.children.size())
        return false;
    for (int i = 0; i < a.children.size(); ++i)
        if (!sameTree(a.children[i], b.children[i]))
            return false;
    return true;
}
bool present(const NativeControl& node, const NativeViewReconciler::Widgets& widgets) {
    if (!widgets.value(node.id))
        return false;
    if (node.kind == NativeControlKind::Button)
        return true; // Menu actions are created when opened, not layout widgets.
    for (const auto& child : node.children)
        if (!present(child, widgets))
            return false;
    return true;
}
void index(const NativeControl& node, const QString& parent, QHash<QString, NativeControl>& nodes,
           QHash<QString, QString>& parents) {
    nodes.insert(node.id, node);
    parents.insert(node.id, parent);
    for (const auto& child : node.children)
        index(child, node.id, nodes, parents);
}
} // namespace
bool NativeViewReconciler::unchanged(const NativeControl& root, const Widgets& widgets) const {
    const auto old = nodes_.constFind(root.id);
    return old != nodes_.cend() && !dirty_.contains(root.id) && sameTree(*old, root) &&
           present(root, widgets);
}
void NativeViewReconciler::begin(const NativeControl& root, const Widgets& widgets) {
    exact_.clear();
    remaining_ = widgets;
    pending_.clear();
    pendingParents_.clear();
    index(root, {}, pending_, pendingParents_);
}
void NativeViewReconciler::consume(const NativeControl& node, Widgets& next) {
    exact_.insert(node.id);
    next.insert(node.id, remaining_.take(node.id));
    if (node.kind == NativeControlKind::Button)
        return;
    for (const auto& child : node.children)
        consume(child, next);
}
QWidget* NativeViewReconciler::take(const NativeControl& node, QWidget* parent, Widgets& next,
                                    bool& exact) {
    exact = false;
    const auto old = nodes_.constFind(node.id);
    auto* widget = remaining_.value(node.id).data();
    if (!widget || old == nodes_.cend() || old->kind != node.kind ||
        old->appearance != node.appearance)
        return nullptr;
    const bool container =
        node.kind == NativeControlKind::Column || node.kind == NativeControlKind::Row ||
        node.kind == NativeControlKind::Scroll || node.kind == NativeControlKind::ChoiceSlider;
    exact = !dirty_.contains(node.id) && sameTree(*old, node) && present(node, remaining_);
    if (!container && !exact)
        return nullptr;
    if (node.kind == NativeControlKind::Label && widget->parentWidget() &&
        (widget->parentWidget()->property("nativeAppearance") == "card") !=
            (parent->property("nativeAppearance") == "card"))
        return nullptr;
    if (widget->parentWidget() != parent) {
        if (auto* viewport = widget->parentWidget())
            if (auto* area = qobject_cast<QScrollArea*>(viewport->parentWidget());
                area && area->widget() == widget)
                area->takeWidget();
        if (auto* previous = widget->parentWidget(); previous && previous->layout())
            previous->layout()->removeWidget(widget);
        widget->setParent(parent);
        widget->setVisible(node.visible);
    }
    if (exact)
        consume(node, next);
    else
        next.insert(node.id, remaining_.take(node.id));
    return widget;
}
QVector<QPointer<QWidget>> NativeViewReconciler::finish() {
    QSet<QWidget*> unused;
    for (const auto& widget : remaining_)
        if (widget)
            unused.insert(widget.data());
    QVector<QPointer<QWidget>> roots;
    for (auto* widget : unused) {
        bool nested = false;
        for (auto* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
            if (unused.contains(parent)) {
                nested = true;
                break;
            }
        if (!nested)
            roots.append(widget);
    }
    remaining_.clear();
    nodes_ = std::move(pending_);
    parents_ = std::move(pendingParents_);
    dirty_.clear();
    return roots;
}
void NativeViewReconciler::invalidateAncestors(const QString& id) {
    for (auto parent = parents_.value(id); !parent.isEmpty(); parent = parents_.value(parent))
        dirty_.insert(parent);
}
bool NativeViewReconciler::textChanged(const QString& id, const QString& text) {
    if (auto node = nodes_.find(id); node != nodes_.end()) {
        if (node->text == text)
            return false;
        node->text = text;
        invalidateAncestors(id);
        return true;
    }
    return false;
}
bool NativeViewReconciler::enabledChanged(const QString& id, bool enabled) {
    if (auto node = nodes_.find(id); node != nodes_.end()) {
        if (node->enabled == enabled)
            return false;
        node->enabled = enabled;
        invalidateAncestors(id);
        return true;
    }
    return false;
}
void NativeViewReconciler::input(const extensions::NativeViewEvent& event) {
    auto node = nodes_.find(event.controlId);
    if (node == nodes_.end())
        return;
    if (event.type == "textChanged" && !node->password)
        node->text = event.text;
    else if (event.type == "selectionChanged")
        node->currentIndex = event.index;
    else if (event.type == "toggled")
        node->checked = event.checked;
    else
        return;
    invalidateAncestors(event.controlId);
}
void NativeViewReconciler::reset() {
    exact_.clear();
    nodes_.clear();
    parents_.clear();
    pending_.clear();
    pendingParents_.clear();
    remaining_.clear();
    dirty_.clear();
}
} // namespace litecode::ui
