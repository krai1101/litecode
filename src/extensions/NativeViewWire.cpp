#include "extensions/NativeViewWire.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
namespace litecode::extensions::wire {
QJsonObject encodeNativeControl(const NativeControl& c) {
    QJsonArray children;
    for (const auto& child : c.children)
        children.append(encodeNativeControl(child));
    QJsonObject result{{"id", c.id},
                       {"kind", int(c.kind)},
                       {"buttonStyle", int(c.buttonStyle)},
                       {"appearance", int(c.appearance)},
                       {"text", c.text},
                       {"placeholder", c.placeholder},
                       {"toolTip", c.toolTip},
                       {"accessibleName", c.accessibleName},
                       {"iconPath", c.iconPath},
                       {"iconSize", c.iconSize},
                       {"items", QJsonArray::fromStringList(c.items)},
                       {"itemIconPaths", QJsonArray::fromStringList(c.itemIconPaths)},
                       {"itemColors", QJsonArray::fromStringList(c.itemColors)},
                       {"helpText", c.helpText},
                       {"helpUrl", c.helpUrl},
                       {"children", children},
                       {"enabled", c.enabled},
                       {"visible", c.visible},
                       {"readOnly", c.readOnly},
                       {"password", c.password},
                       {"checked", c.checked},
                       {"spacing", c.spacing},
                       {"margin", c.margin},
                       {"stretch", c.stretch},
                       {"currentIndex", c.currentIndex},
                       {"value", c.value}};
    if (c.contentsMargins) {
        const auto& m = *c.contentsMargins;
        result.insert("contentsMargins", QJsonArray{m.left(), m.top(), m.right(), m.bottom()});
    }
    return result;
}
namespace {
std::optional<NativeControl> decode(const QJsonObject& o, QSet<QString>& ids, int depth,
                                    int& count, bool menuItem = false) {
    if (depth > 32 || ++count > 4096 || !o.value("id").isString())
        return {};
    NativeControl c;
    c.id = o.value("id").toString();
    if (c.id.isEmpty() || c.id.size() > 256 || ids.contains(c.id))
        return {};
    ids.insert(c.id);
    const auto integer = [&o](const char* key, int min, int max) {
        const auto v = o.value(key);
        return v.isDouble() && v.toDouble() == v.toInt(min - 1) && v.toInt() >= min &&
               v.toInt() <= max;
    };
    if (!integer("kind", 0, int(NativeControlKind::Image)) || !integer("buttonStyle", 0, 3) ||
        !integer("appearance", 0, int(NativeControlAppearance::Shimmer)) ||
        !integer("spacing", 0, 64) || !integer("margin", 0, 64) || !integer("stretch", 0, 100) ||
        !integer("currentIndex", -1, 4095) ||
        !integer("value", 0, (o.value("kind").toInt() == int(NativeControlKind::Image) ||
                                  o.value("kind").toInt() == int(NativeControlKind::Button)) ? 240 : 100))
        return {};
    for (const auto* key : {"text", "placeholder", "toolTip"})
        if (!o.value(key).isString() || o.value(key).toString().size() > 1024 * 1024)
            return {};
    for (const auto* key : {"enabled", "visible", "readOnly", "checked", "password"})
        if (!o.value(key).isBool())
            return {};
    if (!o.value("children").isArray() || !o.value("items").isArray() ||
        o.value("items").toArray().size() > 4096)
        return {};
    if (o.contains("contentsMargins")) {
        if (!o.value("contentsMargins").isArray())
            return {};
        const auto values = o.value("contentsMargins").toArray();
        if (values.size() != 4)
            return {};
        for (const auto& edge : values)
            if (!edge.isDouble() || edge.toDouble() != edge.toInt(-1) || edge.toInt(-1) < 0 ||
                edge.toInt() > 64)
                return {};
        c.contentsMargins =
            QMargins(values[0].toInt(), values[1].toInt(), values[2].toInt(), values[3].toInt());
    }
    c.kind = NativeControlKind(o.value("kind").toInt());
    c.buttonStyle = NativeButtonStyle(o.value("buttonStyle").toInt());
    c.appearance = NativeControlAppearance(o.value("appearance").toInt());
    const bool container =
        c.kind == NativeControlKind::Column || c.kind == NativeControlKind::Row ||
        c.kind == NativeControlKind::Scroll || c.kind == NativeControlKind::Dialog ||
        c.kind == NativeControlKind::ChoiceSlider || c.kind == NativeControlKind::Image ||
        c.kind == NativeControlKind::Button;
    const auto children = o.value("children").toArray();
    if (c.kind == NativeControlKind::Image &&
        (children.size() > 1 ||
         (!children.isEmpty() &&
          children.first().toObject().value("kind").toInt(-1) != int(NativeControlKind::Button))))
        return {};
    if ((!container && !children.isEmpty()) ||
        (c.kind == NativeControlKind::Scroll && children.size() != 1))
        return {};
    for (const auto& value : children) {
        if (!value.isObject())
            return {};
        auto child = decode(value.toObject(), ids, depth + 1, count,
                            c.kind == NativeControlKind::Button);
        if (!child)
            return {};
        if (c.kind == NativeControlKind::Button &&
            (child->kind != NativeControlKind::Button && child->kind != NativeControlKind::Label &&
             child->kind != NativeControlKind::Separator))
            return {};
        c.children.append(std::move(*child));
    }
    for (const auto& value : o.value("items").toArray()) {
        if (!value.isString() || value.toString().size() > 65536)
            return {};
        c.items.append(value.toString());
    }
    if (c.kind == NativeControlKind::Button && !c.children.isEmpty() && !c.items.isEmpty())
        return {};
    const auto readOptions = [&](const char* key, QStringList& result, int limit) {
        if (!o.contains(key))
            return true;
        if (!o.value(key).isArray())
            return false;
        const auto values = o.value(key).toArray();
        if (!values.isEmpty() && values.size() != c.items.size())
            return false;
        for (const auto& value : values) {
            if (!value.isString() || value.toString().size() > limit)
                return false;
            result.append(value.toString());
        }
        return true;
    };
    if (!readOptions("itemIconPaths", c.itemIconPaths, 4096) ||
        !readOptions("itemColors", c.itemColors, 9))
        return {};
    static const QRegularExpression colour("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$");
    for (const auto& value : c.itemColors)
        if (!value.isEmpty() && !colour.match(value).hasMatch())
            return {};
    for (const auto* key : {"helpText", "helpUrl"})
        if (o.contains(key) && (!o.value(key).isString() || o.value(key).toString().size() > 2048))
            return {};
    c.helpText = o.value("helpText").toString();
    c.helpUrl = o.value("helpUrl").toString();
    if (o.value("currentIndex").toInt() >= c.items.size())
        return {};
    c.text = o.value("text").toString();
    c.placeholder = o.value("placeholder").toString();
    c.toolTip = o.value("toolTip").toString();
    if (o.contains("accessibleName") && (!o.value("accessibleName").isString() ||
                                         o.value("accessibleName").toString().size() > 4096))
        return {};
    c.accessibleName = o.value("accessibleName").toString();
    if (o.contains("iconPath") &&
        (!o.value("iconPath").isString() || o.value("iconPath").toString().size() > 4096))
        return {};
    if (o.contains("iconSize") && !integer("iconSize", 8, 64))
        return {};
    c.iconPath = o.value("iconPath").toString();
    c.iconSize = o.value("iconSize").toInt(16);
    if (!c.iconPath.isEmpty() && c.kind != NativeControlKind::Button &&
        c.kind != NativeControlKind::Image && !(menuItem && c.kind == NativeControlKind::Label))
        return {};
    c.enabled = o.value("enabled").toBool();
    c.visible = o.value("visible").toBool();
    c.readOnly = o.value("readOnly").toBool();
    c.checked = o.value("checked").toBool();
    c.password = o.value("password").toBool();
    if (c.password && c.kind != NativeControlKind::Input)
        return {};
    c.spacing = o.value("spacing").toInt();
    c.margin = o.value("margin").toInt();
    c.stretch = o.value("stretch").toInt();
    c.currentIndex = o.value("currentIndex").toInt();
    c.value = o.value("value").toInt();
    return c;
}
} // namespace
std::optional<NativeControl> decodeNativeControl(const QJsonObject& object) {
    QSet<QString> ids;
    int count = 0;
    return decode(object, ids, 0, count);
}
QString validateNativeViewContributions(const QJsonObject& contributions) {
    if (!contributions.contains("views") && !contributions.contains("viewsContainers"))
        return {};
    const auto containers = contributions.value("viewsContainers");
    const auto views = contributions.value("views");
    if (!containers.isObject() || !views.isObject())
        return "Native views require viewsContainers and views objects.";
    QSet<QString> containerIds, viewIds;
    static const QRegularExpression id("^[A-Za-z0-9_.-]{1,128}$");
    const auto containerObject = containers.toObject();
    for (auto it = containerObject.begin(); it != containerObject.end(); ++it) {
        if ((it.key() != "activitybar" && it.key() != "secondarySidebar") || !it.value().isArray())
            return "Unsupported native view container location.";
        for (const auto& value : it.value().toArray()) {
            const auto c = value.toObject();
            const auto name = c.value("id").toString();
            if (!value.isObject() || !id.match(name).hasMatch() || containerIds.contains(name) ||
                c.value("title").toString().trimmed().isEmpty())
                return "Invalid or duplicate native view container.";
            if (c.contains("when"))
                return "Conditional native view containers are not supported yet.";
            containerIds.insert(name);
        }
    }
    const auto viewObject = views.toObject();
    if (containerIds.size() > 1)
        return "An extension may contribute only one native panel container.";
    for (auto it = viewObject.begin(); it != viewObject.end(); ++it) {
        if (!containerIds.contains(it.key()) || !it.value().isArray())
            return "Native view references an unknown container.";
        for (const auto& value : it.value().toArray()) {
            const auto v = value.toObject();
            const auto name = v.value("id").toString();
            if (!value.isObject() || !id.match(name).hasMatch() || viewIds.contains(name) ||
                v.value("type") != QJsonValue("native") ||
                v.value("name").toString().trimmed().isEmpty())
                return "Invalid native view contribution.";
            if (v.contains("when"))
                return "Conditional native views are not supported yet.";
            viewIds.insert(name);
        }
    }
    if (viewIds.size() > 1)
        return "An extension may contribute only one native panel view.";
    return viewIds.isEmpty() ? "No native views contributed." : QString{};
}
} // namespace litecode::extensions::wire
