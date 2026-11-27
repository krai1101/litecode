#include "extensions/WorkbenchExternalUris.h"
#include "extensions/UriWire.h"
#include "extensions/ExtensionWireProtocol.h"
namespace litecode::extensions {
bool WorkbenchExternalUris::accept(const QString& owner, const QJsonObject& message, Send send) {
    bool valid = false; const auto text = message.value("requestId").toString(); const auto id = text.toULongLong(&valid);
    const auto uri = wire::decodeUri(message.value("uri"));
    if (owner.isEmpty() || !send || !valid || !id || QString::number(id) != text || !uri || !message.value("open").isBool()) return false;
    if (pending_.value(owner).contains(id)) return false;
    const bool open = message.value("open").toBool();
    pending_[owner].insert(id, {std::move(send), open});
    if (open && uri->scheme().compare("command", Qt::CaseInsensitive) == 0) {
        complete(owner, id, {}, "Invalid URI scheme 'command'."); return true;
    }
    emit requested({owner, id, *uri, open}); return true;
}
bool WorkbenchExternalUris::complete(const QString& owner, quint64 id, const QJsonValue& result, const QString& error) {
    auto requests = pending_.find(owner); if (requests == pending_.end()) return false;
    auto found = requests->find(id); if (found == requests->end()) return false;
    if (error.isEmpty() && !(found->open ? result.isBool() : wire::decodeUri(result).has_value())) return false;
    auto send = std::move(found->send); requests->erase(found); if (requests->isEmpty()) pending_.erase(requests);
    QJsonObject reply{{"command", "externalUriResult"}, {"requestId", QString::number(id)}, {"result", result}, {"error", error}};
    if (wire::encode(reply).isEmpty()) reply = {{"command", "externalUriResult"}, {"requestId", QString::number(id)}, {"error", "External URI result exceeds the transport limit."}};
    try { send(reply); } catch (...) {}
    return true;
}
void WorkbenchExternalUris::removeOwner(const QString& owner) {
    const auto pending = pending_.take(owner);
    for (auto request = pending.cbegin(); request != pending.cend(); ++request) {
        try { request->send({{"command", "externalUriResult"}, {"requestId", QString::number(request.key())}, {"error", "Extension host stopped before URI operation completed."}}); } catch (...) {}
    }
}
}
