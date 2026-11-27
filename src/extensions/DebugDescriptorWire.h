#pragma once
#include "extensions/DebugApi.h"
#include <QJsonObject>
namespace litecode::extensions::wire {
// This endpoint is private IPC, not a public named-pipe adapter descriptor.
// The implementation and its disposal remain owned by the extension host.
struct InlineDebugAdapterEndpoint final { QString path; };
using DebugAdapterWireDescriptor = std::variant<DebugAdapterExecutable, DebugAdapterServer,
    DebugAdapterNamedPipeServer, InlineDebugAdapterEndpoint>;
QJsonObject encodeDebugAdapterDescriptor(const DebugAdapterDescriptor& descriptor);
std::optional<DebugAdapterWireDescriptor> decodeDebugAdapterDescriptor(const QJsonObject& value);
} // namespace litecode::extensions::wire
