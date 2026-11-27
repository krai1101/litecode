#include "extensions/WorkbenchOutput.h"
#include <QPointer>
#include <algorithm>

namespace litecode::extensions {
bool WorkbenchOutput::accept(const QString& owner, const QJsonObject& message) {
    const auto id = message.value("id").toString(),
               operation = message.value("operation").toString();
    if (id.isEmpty() || id.size() > 128)
        return false;
    if (operation == "create") {
        const auto name = message.value("name"), language = message.value("languageId"),
                   level = message.value("level");
        if (channels_.contains(id) || !name.isString() || name.toString().trimmed().isEmpty() ||
            !language.isString() || !message.value("log").isBool() || !level.isDouble() ||
            level.toDouble() != level.toInt(-1) || level.toInt(-1) < 0 || level.toInt() > 5)
            return false;
        channels_.insert(
            id,
            {id,
             owner,
             name.toString(),
             language.toString(),
             {},
             message.value("log").toBool(),
             message.value("log").toBool() ? globalLevel_ : static_cast<LogLevel>(level.toInt())});
        emit channelsChanged();
        return true;
    }
    auto channel = channels_.find(id);
    if (channel == channels_.end() || channel->owner != owner)
        return false;
    if (operation == "append") {
        if (!message.value("text").isString() || message.value("text").toString().size() > 32768)
            return false;
        const auto text = message.value("text").toString();
        channel->text += text;
        emit textChanged(id, text, true);
    } else if (operation == "clear") {
        channel->text.clear();
        emit textChanged(id, {}, false);
    } else if (operation == "show") {
        if (!message.value("preserveFocus").isBool())
            return false;
        emit revealRequested(id, message.value("preserveFocus").toBool());
    } else if (operation == "hide")
        emit hideRequested(id);
    else if (operation == "dispose") {
        levelOverrides_.remove(id);
        channels_.erase(channel);
        emit channelsChanged();
    } else if (operation == "error") {
        if (!message.value("text").isString())
            return false;
        emit channelError(message.value("text").toString());
    } else
        return false;
    return true;
}
void WorkbenchOutput::removeOwner(const QString& owner) {
    bool removed = false;
    for (auto it = channels_.begin(); it != channels_.end();) {
        if (it->owner == owner) {
            levelOverrides_.remove(it.key());
            it = channels_.erase(it);
            removed = true;
        } else
            ++it;
    }
    if (removed)
        emit channelsChanged();
}
QVector<OutputChannelInfo> WorkbenchOutput::channels() const {
    auto result = channels_.values();
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        const auto order = QString::localeAwareCompare(a.name, b.name);
        return order == 0 ? a.id < b.id : order < 0;
    });
    return result;
}
std::optional<OutputChannelInfo> WorkbenchOutput::channel(const QString& id) const {
    const auto entry = channels_.constFind(id);
    return entry == channels_.cend() ? std::nullopt : std::optional<OutputChannelInfo>(*entry);
}
void WorkbenchOutput::setLogLevel(const QString& id, LogLevel level) {
    if (static_cast<int>(level) < 0 || static_cast<int>(level) > 5)
        return;
    auto channel = channels_.find(id);
    if (channel == channels_.end() || !channel->log)
        return;
    const auto next = level == globalLevel_ ? std::nullopt : std::optional(level);
    const auto current =
        levelOverrides_.contains(id) ? std::optional(levelOverrides_.value(id)) : std::nullopt;
    if (channel->level == level && current == next)
        return;
    if (next)
        levelOverrides_.insert(id, *next);
    else
        levelOverrides_.remove(id);
    channel->level = level;
    const auto owner = channel->owner;
    const QPointer<WorkbenchOutput> alive(this);
    emit logLevelRequested(owner, id, static_cast<int>(level));
    if (alive)
        emit channelsChanged();
}
void WorkbenchOutput::setGlobalLogLevel(LogLevel level) {
    if (static_cast<int>(level) < 0 || static_cast<int>(level) > 5 || globalLevel_ == level)
        return;
    globalLevel_ = level;
    for (auto& channel : channels_)
        if (channel.log && !levelOverrides_.contains(channel.id))
            channel.level = level;
    const QPointer<WorkbenchOutput> alive(this);
    emit globalLogLevelChanged(static_cast<int>(level));
    if (alive)
        emit channelsChanged();
}
void WorkbenchOutput::clear(const QString& id) {
    const auto channel = channels_.constFind(id);
    if (channel == channels_.cend())
        return;
    const auto owner = channel->owner;
    emit clearRequested(owner, id);
}
} // namespace litecode::extensions
