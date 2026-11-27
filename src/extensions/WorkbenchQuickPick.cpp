#include "extensions/WorkbenchQuickPick.h"
#include "extensions/QuickPickWire.h"
#include <QPointer>
#include <QSet>
namespace litecode::extensions {
bool WorkbenchQuickPick::accept(const QString& owner, const QJsonObject& message, Send send) {
    bool valid = false; const auto text = message.value("requestId").toString(); const auto id = text.toULongLong(&valid);
    if (owner.isEmpty() || !valid || !id || QString::number(id) != text || !send) return false;
    const auto op = message.value("op").toString();
    if (op == "show") {
        if (!message.value("options").isObject()) return false;
        const auto options = wire::decodeQuickPickOptions(message.value("options").toObject());
        if (!options || pending_.value(owner).contains(id)) return false;
        QuickPickRequest request{owner, id, *options};
        pending_[owner].insert(id, {request, std::move(send), {}, false}); emit requested(request); return true;
    }
    if (op == "cancel") { complete(owner, id, {}); return true; }
    if (op != "items" || !message.value("items").isArray()) return false;
    const auto items = wire::decodeQuickPickItems(message.value("items").toArray()); if (!items) return false;
    auto ownerRequests = pending_.find(owner); if (ownerRequests == pending_.end()) return true;
    auto found = ownerRequests->find(id); if (found == ownerRequests->end()) return true;
    found->items = *items; found->loaded = true; emit itemsChanged(owner, id, *items); return true;
}
bool WorkbenchQuickPick::complete(const QString& owner, quint64 id, std::optional<QVector<int>> selection, const QString& error) {
    auto requests = pending_.find(owner); if (requests == pending_.end()) return false;
    auto found = requests->find(id); if (found == requests->end()) return false;
    if (selection && error.isEmpty()) {
        if (!found->loaded || (!found->request.options.canPickMany.value_or(false) && selection->size() != 1)) return false;
        QSet<int> unique;
        for (const auto index : *selection) {
            if (index < 0 || index >= found->items.size() || unique.contains(index) ||
                found->items.at(index).kind.value_or(QuickPickItemKind::Default) == QuickPickItemKind::Separator) return false;
            unique.insert(index);
        }
    }
    auto pending = std::move(*found); requests->erase(found); if (requests->isEmpty()) pending_.erase(requests);
    QJsonValue result(QJsonValue::Null);
    if (selection) { QJsonArray values; for (const auto index : *selection) values.append(index); result = values; }
    emit dismissed(owner, id);
    try { pending.send({{"command", "quickPick"}, {"op", "result"}, {"requestId", QString::number(id)}, {"selection", result}, {"error", error}}); }
    catch (...) {}
    return true;
}
bool WorkbenchQuickPick::focus(const QString& owner, quint64 id, int index) {
    auto requests = pending_.constFind(owner); if (requests == pending_.cend()) return false;
    auto found = requests->constFind(id); if (found == requests->cend() || !found->loaded || index < 0 || index >= found->items.size() ||
        found->items.at(index).kind.value_or(QuickPickItemKind::Default) == QuickPickItemKind::Separator) return false;
    const auto send = found->send;
    try { send({{"command", "quickPick"}, {"op", "focus"}, {"requestId", QString::number(id)}, {"selection", index}}); }
    catch (...) { return false; }
    return true;
}
void WorkbenchQuickPick::removeOwner(const QString& owner) {
    const auto pending = pending_.take(owner); const QPointer<WorkbenchQuickPick> self(this);
    for (const auto& request : pending) {
        if (self) emit self->dismissed(owner, request.request.id);
        try { request.send({{"command", "quickPick"}, {"op", "result"}, {"requestId", QString::number(request.request.id)},
            {"selection", QJsonValue(QJsonValue::Null)}, {"error", "Extension host stopped before quick pick completed."}}); } catch (...) {}
    }
}
}
