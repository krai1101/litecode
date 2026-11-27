#include "extensions/WorkbenchMessages.h"
#include <QJsonArray>
#include <limits>
#include <utility>

namespace litecode::extensions {
bool WorkbenchMessages::accept(const QString& owner, const QString& source,
                               const QJsonObject& message, Reply reply) {
    bool valid = false;
    const auto encoded = message.value("requestId").toString();
    const auto id = encoded.toULongLong(&valid);
    const auto severity = message.value("severity");
    if (owner.isEmpty() || !valid || id == 0 || QString::number(id) != encoded ||
        !severity.isDouble() || severity.toDouble() != severity.toInt(-1) || severity.toInt() < 1 ||
        severity.toInt() > 3 || !message.value("message").isString() ||
        !message.value("modal").isBool() || !message.value("items").isArray() ||
        (message.contains("detail") && !message.value("detail").isString()) || !reply)
        return false;
    const auto ownerRequests = pending_.constFind(owner);
    if (ownerRequests != pending_.cend() && ownerRequests->contains(id))
        return false;
    const auto items = message.value("items").toArray();
    if (items.size() > std::numeric_limits<int>::max())
        return false;
    WindowMessageRequest request{
        owner, source, message.value("message").toString(), id, severity.toInt(), {}, {}};
    request.options.modal = message.value("modal").toBool();
    if (message.contains("detail"))
        request.options.detail = message.value("detail").toString();
    request.items.reserve(items.size());
    for (const auto& item : items) {
        if (!item.isObject())
            return false;
        const auto action = item.toObject();
        if (!action.value("title").isString() || !action.value("close").isBool())
            return false;
        request.items.append({action.value("title").toString(), action.value("close").toBool()});
    }
    pending_[owner].insert(id, {request, std::move(reply)});
    emit requested(request);
    return true;
}
bool WorkbenchMessages::select(const QString& owner, quint64 id, std::optional<int> selection) {
    auto requests = pending_.find(owner);
    if (requests == pending_.end())
        return false;
    auto found = requests->find(id);
    if (found == requests->end())
        return false;
    if (selection && (*selection < 0 || *selection >= found->request.items.size()))
        return false;
    auto pending = std::move(*found);
    requests->erase(found);
    if (requests->isEmpty())
        pending_.erase(requests);
    emit dismissed(owner, id);
    pending.reply(selection, {});
    return true;
}
void WorkbenchMessages::removeOwner(const QString& owner) {
    const auto requests = pending_.take(owner);
    for (const auto& pending : requests) {
        emit dismissed(owner, pending.request.id);
        pending.reply({}, "Extension host stopped before message completed.");
    }
}
} // namespace litecode::extensions
