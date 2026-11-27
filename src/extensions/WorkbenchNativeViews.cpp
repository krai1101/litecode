#include "extensions/WorkbenchNativeViews.h"
#include "extensions/NativeViewWire.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
namespace litecode::extensions {
namespace {
bool resolvePackageIcon(QString& value, const QString& root) {
    if (value.isEmpty())
        return true;
    const QFileInfo path(QDir(root).filePath(value));
    const auto canonical = path.canonicalFilePath();
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    if (root.isEmpty() || canonical.isEmpty() || !path.isFile() ||
        !canonical.startsWith(root + '/', sensitivity) || path.size() > 1024 * 1024)
        return false;
    value = canonical;
    return true;
}
bool resolveViewIcons(NativeControl& control, const QString& root) {
    if (!control.iconPath.isEmpty()) {
        const auto path = QFileInfo(QDir(root).filePath(control.iconPath));
        if (control.kind == NativeControlKind::Image) {
            // Images can be user attachments outside the extension package.
            // Missing historical files render an unavailable thumbnail.
            if (!QDir::isAbsolutePath(control.iconPath) || control.iconPath.contains(QChar::Null))
                return false;
            // The thumbnail loader handles unreadable/oversized images as an
            // unavailable preview, without rejecting the entire view frame.
        } else if (!resolvePackageIcon(control.iconPath, root))
            return false;
    }
    for (auto& icon : control.itemIconPaths)
        if (!resolvePackageIcon(icon, root))
            return false;
    for (auto& child : control.children)
        if (!resolveViewIcons(child, root))
            return false;
    return true;
}
} // namespace
void WorkbenchNativeViews::setContributions(const QVector<NativeViewContribution>& contributions) {
    QSet<QString> retained;
    bool changed = false;
    for (const auto& contribution : contributions) {
        if (retained.contains(contribution.id))
            continue;
        retained.insert(contribution.id);
        auto it = entries_.find(contribution.id);
        if (it != entries_.end() && it->contribution.owner == contribution.owner)
            continue;
        if (it != entries_.end())
            emit unavailable(contribution.id, "Native view owner changed.");
        Entry entry;
        entry.contribution = contribution;
        entries_.insert(contribution.id, std::move(entry));
        changed = true;
    }
    const auto ids = entries_.keys();
    for (const auto& id : ids)
        if (!retained.contains(id)) {
            entries_.remove(id);
            changed = true;
            emit unavailable(id, "Extension is disabled or unavailable.");
        }
    if (changed)
        emit contributionsChanged();
}
QVector<NativeViewContribution> WorkbenchNativeViews::contributions() const {
    QVector<NativeViewContribution> result;
    for (const auto& entry : entries_)
        result.append(entry.contribution);
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
void WorkbenchNativeViews::send(Entry& entry, QJsonObject message) {
    message.insert("command", "nativeView");
    message.insert("id", entry.contribution.id);
    message.insert("handle", entry.handle);
    emit sendRequested(entry.contribution.owner, message);
}
void WorkbenchNativeViews::setVisible(const QString& id, bool visible) {
    auto it = entries_.find(id);
    if (it == entries_.end())
        return;
    it->visible = visible;
    if (visible && it->handle.isEmpty()) {
        const auto owner = it->contribution.owner;
        emit activationRequested(owner);
        return;
    }
    if (it->handle.isEmpty())
        return;
    if (!it->resolved && visible) {
        it->resolved = true;
        send(*it, {{"action", "resolve"}});
    } else if (it->resolved)
        send(*it, {{"action", "visibility"}, {"visible", visible}});
}
bool WorkbenchNativeViews::accept(const QString& owner, const QJsonObject& message) {
    const auto id = message.value("id").toString();
    auto it = entries_.find(id);
    if (it == entries_.end() || it->contribution.owner != owner)
        return false;
    const auto handle = message.value("handle").toString(),
               action = message.value("action").toString();
    bool numeric{};
    const auto number = handle.toULongLong(&numeric);
    if (!numeric || !number || QString::number(number) != handle)
        return false;
    if (action == "register") {
        if (!it->handle.isEmpty())
            return false;
        it->inputs = {};
        it->handle = handle;
        if (it->visible) {
            it->resolved = true;
            send(*it, {{"action", "resolve"}});
        }
        return true;
    }
    if (it->handle != handle)
        return true; // Ignore a late message from an earlier provider.
    if (action == "unregister") {
        it->handle.clear();
        it->controls.clear();
        it->resolved = false;
        ++it->revision;
        emit unavailable(id, "Native view provider was disposed.");
        return true;
    }
    if (action == "show" && message.value("preserveFocus").isBool()) {
        emit showRequested(id, message.value("preserveFocus").toBool());
        return true;
    }
    if (!it->resolved)
        return false;
    if (message.contains("inputSequence") &&
        !it->inputs.acknowledge(message.value("inputSequence").toString().toULongLong()))
        return true; // A stale or invalid acknowledgement cannot roll input backwards.
    if (action == "content") {
        auto root = wire::decodeNativeControl(message.value("root").toObject());
        if (!root)
            return false;
        const auto extensionRoot = QFileInfo(it->contribution.extensionRoot).canonicalFilePath();
        if (!resolveViewIcons(*root, extensionRoot))
            return false;
        it->inputs.reconcile(*root);
        it->controls.clear();
        const auto collect = [&it](auto&& self, const NativeControl& c) -> void {
            it->controls.insert(c.id, c.kind);
            for (const auto& child : c.children)
                self(self, child);
        };
        collect(collect, *root);
        ++it->revision;
        emit contentChanged(id, *root, it->revision);
        return true;
    }
    if (action == "error" && message.value("text").isString()) {
        emit unavailable(id, message.value("text").toString());
        return true;
    }
    const auto control = message.value("control").toString();
    if (!it->controls.contains(control))
        return false;
    if (action == "scrollToEnd") {
        if (it->controls.value(control) != NativeControlKind::Scroll) return false;
        emit scrollToEndRequested(id, control);
        return true;
    }
    if (action == "text" && message.value("text").isString() &&
        message.value("text").toString().size() <= 1024 * 1024) {
        if (!it->inputs.pending(control))
            emit textChanged(id, control, message.value("text").toString());
        return true;
    }
    if (action == "enabled" && message.value("enabled").isBool()) {
        emit enabledChanged(id, control, message.value("enabled").toBool());
        return true;
    }
    return false;
}
void WorkbenchNativeViews::removeOwner(const QString& owner, const QString& error) {
    const auto ids = entries_.keys();
    for (const auto& id : ids) {
        auto& entry = entries_[id];
        if (entry.contribution.owner != owner)
            continue;
        entry.handle.clear();
        entry.resolved = false;
        entry.controls.clear();
        ++entry.revision;
        emit unavailable(id, error);
    }
}
quint64 WorkbenchNativeViews::revision(const QString& id) const {
    return entries_.value(id).revision;
}
bool WorkbenchNativeViews::reveal(const QString& id) {
    if (!entries_.contains(id)) return false;
    emit showRequested(id, false);
    return true;
}
void WorkbenchNativeViews::input(const QString& id, quint64 revision,
                                 const NativeViewEvent& event) {
    auto it = entries_.find(id);
    if (it == entries_.end() || it->handle.isEmpty() || !it->resolved || !it->visible ||
        it->revision != revision || !it->controls.contains(event.controlId) ||
        event.text.size() > 1024 * 1024)
        return;
    const auto kind = it->controls.value(event.controlId);
    const bool allowed =
        (kind == NativeControlKind::Button &&
         (event.type == "clicked" || event.type == "selected")) ||
        ((kind == NativeControlKind::Input || kind == NativeControlKind::TextEdit) &&
         (event.type == "textChanged" || event.type == "submitted")) ||
        (kind == NativeControlKind::TextEdit && event.type == "filesPasted") ||
        ((kind == NativeControlKind::ComboBox || kind == NativeControlKind::ChoiceSlider ||
          kind == NativeControlKind::List) &&
         event.type == "selectionChanged") ||
        (kind == NativeControlKind::CheckBox && event.type == "toggled") ||
        (kind == NativeControlKind::Dialog && event.type == "dismissed") ||
        (kind == NativeControlKind::Markdown && event.type == "linkActivated") ||
        (kind == NativeControlKind::Scroll &&
         (event.type == "scrollStart" || event.type == "scrollEnd" || event.type == "scrollToEnd"));
    if (!allowed)
        return;
    const auto sequence = it->inputs.record(event);
    send(*it, {{"action", "input"},
               {"inputSequence", QString::number(sequence)},
               {"control", event.controlId},
               {"type", event.type},
               {"text", event.text},
               {"index", event.index},
               {"checked", event.checked}});
}
} // namespace litecode::extensions
