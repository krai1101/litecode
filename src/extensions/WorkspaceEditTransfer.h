#pragma once
#include "extensions/JsonMessageTransfer.h"

namespace litecode::extensions::wire {
// Private framed transport. Only a fully reconstructed request is dispatched;
// partial edits never reach document/file services. One transfer per host.
inline constexpr qsizetype WorkspaceEditChunkBytes = JsonTransferChunkBytes;
inline constexpr qsizetype MaxWorkspaceEditTransferBytes = MaxJsonTransferBytes;
void sendWorkspaceEdit(const QJsonObject& request, const std::function<void(const QJsonObject&)>& send);
class WorkspaceEditTransferDecoder final {
  public:
    std::optional<QJsonObject> accept(const QJsonObject& message, QString* error) { return decoder_.accept(message, error); }
    [[nodiscard]] bool transferring() const { return decoder_.transferring(); }
    void reset() { decoder_.reset(); }
  private:
    JsonMessageTransferDecoder decoder_{{"event", "workspaceApplyEdit", "workspaceEdit"}};
};
} // namespace litecode::extensions::wire
