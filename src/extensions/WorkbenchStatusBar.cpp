#include "extensions/WorkbenchStatusBar.h"
#include <algorithm>
#include <limits>
#include <type_traits>
namespace litecode::extensions {
namespace {
QString entryKey(const QString& owner, quint64 token, const std::optional<QString>& itemId) {
    return owner + QChar(0) +
           (itemId && !itemId->isEmpty() ? "id:" + *itemId : "token:" + QString::number(token));
}
QString messageKey(const QString& owner, quint64 token) {
    return owner + QChar(0) + QString::number(token);
}
} // namespace
bool WorkbenchStatusBar::accept(const QString& owner, const QString& displayName,
                                const QJsonObject& message) {
    if (owner.isEmpty())
        return false;
    const auto op = message.value("op").toString();
    if (op == "error") {
        if (!message.value("text").isString())
            return false;
        emit error(message.value("text").toString());
        return true;
    }
    bool valid = false;
    const auto text = message.value("token").toString();
    const auto token = text.toULongLong(&valid);
    if (!valid || !token || QString::number(token) != text)
        return false;
    if (op == "set") {
        if (!message.value("state").isObject())
            return false;
        const auto state = wire::decodeStatusBarState(message.value("state").toObject());
        if (!state)
            return false;
        const auto key = entryKey(owner, token, state->itemId);
        auto found = entries_.find(key);
        quint64 order = 0;
        if (found != entries_.end())
            order = found->order;
        else {
            if (nextOrder_ == std::numeric_limits<quint64>::max())
                return false;
            order = ++nextOrder_;
        }
        const auto id =
            state->itemId && !state->itemId->isEmpty() ? owner + "." + *state->itemId : owner;
        const auto name =
            state->name && !state->name->isEmpty()
                ? *state->name
                : tr("%1 (Extension)").arg(displayName.isEmpty() ? owner : displayName);
        entries_.insert(key, {key, owner, id, name, token, order, *state});
        emit changed();
        return true;
    }
    if (op == "remove") {
        std::optional<QString> itemId;
        if (message.contains("itemId")) {
            if (!message.value("itemId").isString())
                return false;
            itemId = message.value("itemId").toString();
        }
        if (entries_.remove(entryKey(owner, token, itemId)))
            emit changed();
        return true;
    }
    const auto key = messageKey(owner, token);
    if (op == "message") {
        if (!message.value("text").isString() || messages_.contains(key) ||
            nextOrder_ == std::numeric_limits<quint64>::max())
            return false;
        messages_.insert(key, {owner, message.value("text").toString(), token, ++nextOrder_});
        emit changed();
        return true;
    }
    if (op == "messageRemove") {
        if (messages_.remove(key))
            emit changed();
        return true;
    }
    return false;
}
void WorkbenchStatusBar::removeOwner(const QString& owner) {
    bool removed = false;
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        if (entry->owner == owner) {
            entry = entries_.erase(entry);
            removed = true;
        } else
            ++entry;
    }
    for (auto message = messages_.begin(); message != messages_.end();) {
        if (message->owner == owner) {
            message = messages_.erase(message);
            removed = true;
        } else
            ++message;
    }
    if (removed)
        emit changed();
}
QVector<StatusBarEntryInfo> WorkbenchStatusBar::entries() const {
    QVector<StatusBarEntryInfo> result;
    result.reserve(entries_.size() + 1);
    for (const auto& entry : entries_)
        result.append(entry);
    const Message* top = nullptr;
    for (const auto& message : messages_)
        if (!top || message.order > top->order)
            top = &message;
    if (top) {
        wire::StatusBarState state;
        state.text = top->text;
        state.priority = std::numeric_limits<double>::denorm_min();
        result.append({"status.extensionMessage", top->owner, "status.extensionMessage",
                       tr("Extension Status"), top->token, top->order, state});
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const StatusBarEntryInfo& left, const StatusBarEntryInfo& right) {
                         if (left.state.alignment != right.state.alignment)
                             return left.state.alignment == StatusBarAlignment::Left;
                         const auto leftPriority = left.state.priority.value_or(0),
                                    rightPriority = right.state.priority.value_or(0);
                         if (leftPriority != rightPriority)
                             return leftPriority > rightPriority;
                         return left.order < right.order;
                     });
    return result;
}
std::optional<StatusBarEntryInfo> WorkbenchStatusBar::entry(const QString& key) const {
    const auto found = entries_.constFind(key);
    if (found != entries_.cend())
        return *found;
    if (key == "status.extensionMessage")
        for (const auto& entry : entries())
            if (entry.key == key)
                return entry;
    return {};
}
bool WorkbenchStatusBar::invoke(const QString& key) {
    const auto info = entry(key);
    if (!info || !info->state.command)
        return false;
    QString command;
    QJsonArray arguments;
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QString>)
                command = value;
            else {
                command = value.command;
                arguments = value.arguments;
            }
        },
        *info->state.command);
    if (command.isEmpty())
        return false;
    emit commandRequested(command, arguments);
    return true;
}
} // namespace litecode::extensions
