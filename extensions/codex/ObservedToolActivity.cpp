#include "ObservedToolActivity.h"
#include "ToolCallDisplay.h"
#include <QJsonDocument>

namespace litecode::codex {
void ObservedToolActivity::readOutputPrefix(const QByteArray& prefix) {
    // The rollout serializer puts output after call_id. Decode that small
    // envelope only; an image result may contain megabytes of base64 data.
    const auto output = prefix.indexOf("\"output\"");
    if (output < 0)
        return;
    const auto record =
        QJsonDocument::fromJson(prefix.first(output) + "\"output\":null}}").object();
    const auto type = record.value("payload").toObject().value("type").toString();
    if (type == "function_call_output" || type == "custom_tool_call_output")
        read(record);
}
void ObservedToolActivity::read(const QJsonObject& record) {
    if (record.value("type") != "response_item")
        return;
    const auto payload = record.value("payload").toObject();
    const auto type = payload.value("type").toString();
    const auto id = payload.value("call_id").toString();
    if (id.isEmpty())
        return;
    if (type == "function_call_output" || type == "custom_tool_call_output") {
        completed_.insert(id);
        return;
    }
    if ((type != "function_call" && type != "custom_tool_call") || completed_.contains(id))
        return;
    completed_.insert(id); // A repeated projection of the same call is not another tool.
    auto item = toolCallDisplay(payload);
    item.insert("id", id);
    item.insert("status", "inProgress");
    pending_.prepend(item);
}
QJsonArray ObservedToolActivity::pending() const {
    return pending_;
}
} // namespace litecode::codex
