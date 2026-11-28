#include "extensions/WorkbenchDialogs.h"
#include "extensions/DialogWire.h"
#include <utility>

namespace litecode::extensions {
bool WorkbenchDialogs::accept(const QString& owner, const QJsonObject& message, Reply reply) {
    bool valid = false;
    const auto encoded = message.value("requestId").toString();
    const auto id = encoded.toULongLong(&valid);
    if (owner.isEmpty() || !valid || !id || QString::number(id) != encoded || !reply ||
        !message.value("save").isBool() || !message.value("options").isObject())
        return false;
    const auto found = pending_.constFind(owner);
    if (found != pending_.cend() && found->contains(id))
        return false;
    FileDialogRequest request{owner, id, OpenDialogOptions{}};
    if (message.value("save").toBool()) {
        const auto options = wire::decodeSaveDialogOptions(message.value("options").toObject());
        if (!options)
            return false;
        request.options = *options;
    } else {
        const auto options = wire::decodeOpenDialogOptions(message.value("options").toObject());
        if (!options)
            return false;
        request.options = *options;
    }
    pending_[owner].insert(id, {request, std::move(reply)});
    emit requested(request);
    return true;
}
bool WorkbenchDialogs::complete(const QString& owner, quint64 id,
                                std::optional<QVector<QUrl>> selection, const QString& error) {
    auto requests = pending_.find(owner);
    if (requests == pending_.end())
        return false;
    auto found = requests->find(id);
    if (found == requests->end())
        return false;
    if (selection && error.isEmpty()) {
        const auto* open = std::get_if<OpenDialogOptions>(&found->request.options);
        if (selection->isEmpty() ||
            ((!open || !open->canSelectMany.value_or(false)) && selection->size() != 1))
            return false;
        for (const auto& uri : *selection)
            if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty())
                return false;
    }
    auto pending = std::move(*found);
    requests->erase(found);
    if (requests->isEmpty())
        pending_.erase(requests);
    emit dismissed(owner, id);
    pending.reply(std::move(selection), error);
    return true;
}
void WorkbenchDialogs::removeOwner(const QString& owner) {
    const auto pending = pending_.take(owner);
    for (const auto& request : pending) {
        emit dismissed(owner, request.request.id);
        request.reply({}, "Extension host stopped before file dialog completed.");
    }
}
} // namespace litecode::extensions
