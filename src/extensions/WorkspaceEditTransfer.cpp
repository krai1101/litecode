#include "extensions/WorkspaceEditTransfer.h"
namespace litecode::extensions::wire {
void sendWorkspaceEdit(const QJsonObject& request, const std::function<void(const QJsonObject&)>& send) {
    sendJsonTransfer(request, {"event", "workspaceApplyEdit", "workspaceEdit"}, send);
}
} // namespace litecode::extensions::wire
