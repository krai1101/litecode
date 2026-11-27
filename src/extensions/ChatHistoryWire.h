#pragma once

#include "extensions/ExtensionApi.h"
#include <QJsonArray>
#include <optional>

namespace litecode::extensions::wire {

// Decode the private workbench history into the stable chat turn value shapes.
// A session contains ordered request/response pairs for one participant only.
[[nodiscard]] std::optional<ChatContext> decodeChatContext(
    const QJsonArray& history, const QString& participantId);

} // namespace litecode::extensions::wire
