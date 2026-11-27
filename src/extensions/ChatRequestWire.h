#pragma once

#include "extensions/ExtensionApi.h"
#include <QJsonObject>
#include <optional>

namespace litecode::extensions::wire {

// Private workbench/host representation. A reference's sibling field
// $litecodeValueType is "uri" or "location" when its value is a resource;
// untagged JSON values remain opaque. Prompt range offsets are
// UTF-16 indices, matching QString and JavaScript strings.
[[nodiscard]] std::optional<QJsonObject> normalizeChatRequest(const QJsonObject& input);
[[nodiscard]] std::optional<ChatRequest> decodeChatRequest(const QJsonObject& input);

} // namespace litecode::extensions::wire
