#include "extensions/WorkbenchInputBox.h"
#include "extensions/InputBoxWire.h"
#include <QPointer>
#include <limits>
namespace litecode::extensions {
namespace {
quint64 identifier(const QJsonValue& value) {
    if (!value.isString()) return 0;
    bool valid = false;
    const auto text = value.toString();
    const auto id = text.toULongLong(&valid);
    return valid && id && QString::number(id) == text ? id : 0;
}
}
bool WorkbenchInputBox::accept(const QString& owner, const QJsonObject& message, Send send) {
    const auto id = identifier(message.value("requestId"));
    if (owner.isEmpty() || !id || !send) return false;
    const auto op = message.value("op").toString();
    if (op == "show") {
        if (!message.value("options").isObject() || !message.value("hasValidation").isBool()) return false;
        const auto options = wire::decodeInputBoxOptions(message.value("options").toObject());
        if (!options || pending_.value(owner).contains(id)) return false;
        InputBoxRequest request{owner, id, *options, message.value("hasValidation").toBool()};
        pending_[owner].insert(id, {request, std::move(send), 0, {}});
        emit requested(request); return true;
    }
    if (op == "cancel") { complete(owner, id, {}); return true; }
    if (op != "validation" || !message.value("error").isString()) return false;
    const auto sequence = identifier(message.value("sequence"));
    const auto result = wire::decodeInputBoxValidation(message.value("result"));
    if (!sequence || !result) return false;
    auto requests = pending_.find(owner);
    if (requests == pending_.end()) return true;
    auto found = requests->find(id);
    if (found == requests->end() || !found->validations.remove(sequence)) return true;
    emit validated(owner, id, sequence, *result, message.value("error").toString()); return true;
}
quint64 WorkbenchInputBox::validate(const QString& owner, quint64 id, const QString& value) {
    auto requests = pending_.find(owner);
    if (requests == pending_.end()) return 0;
    auto found = requests->find(id);
    if (found == requests->end() || !found->request.hasValidation || found->nextSequence == std::numeric_limits<quint64>::max()) return 0;
    const auto sequence = ++found->nextSequence;
    found->validations.insert(sequence);
    const auto send = found->send;
    const QPointer<WorkbenchInputBox> self(this);
    QMetaObject::invokeMethod(this, [self, send, owner, id, sequence, value] {
        if (!self) return;
        const auto requests = self->pending_.constFind(owner);
        if (requests == self->pending_.cend()) return;
        const auto found = requests->constFind(id);
        if (found == requests->cend() || !found->validations.contains(sequence)) return;
        try { send({{"command", "inputBox"}, {"op", "validate"}, {"requestId", QString::number(id)},
            {"sequence", QString::number(sequence)}, {"value", value}}); }
        catch (...) { if (self) self->complete(owner, id, {}, "Could not send input validation request."); }
    }, Qt::QueuedConnection);
    return sequence;
}
bool WorkbenchInputBox::complete(const QString& owner, quint64 id, std::optional<QString> value, const QString& error) {
    auto requests = pending_.find(owner);
    if (requests == pending_.end()) return false;
    auto found = requests->find(id);
    if (found == requests->end()) return false;
    auto pending = std::move(*found);
    requests->erase(found);
    if (requests->isEmpty()) pending_.erase(requests);
    emit dismissed(owner, id);
    try { pending.send({{"command", "inputBox"}, {"op", "result"}, {"requestId", QString::number(id)},
        {"value", value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null)}, {"error", error}}); }
    catch (...) { /* The original host socket may already have closed. */ }
    return true;
}
void WorkbenchInputBox::removeOwner(const QString& owner) {
    const auto requests = pending_.take(owner);
    const QPointer<WorkbenchInputBox> self(this);
    for (const auto& pending : requests) {
        if (self) emit self->dismissed(owner, pending.request.id);
        try { pending.send({{"command", "inputBox"}, {"op", "result"}, {"requestId", QString::number(pending.request.id)},
            {"value", QJsonValue(QJsonValue::Null)}, {"error", "Extension host stopped before input box completed."}}); }
        catch (...) {}
    }
}
} // namespace litecode::extensions
