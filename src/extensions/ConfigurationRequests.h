#pragma once
#include <QJsonObject>
#include <QString>

namespace litecode::extensions {
class ConfigurationService;
// Private host-request adapter. Returns a structured error to the caller;
// configuration failures must not crash or disable a functioning extension.
[[nodiscard]] QString applyConfigurationUpdate(ConfigurationService& service,
                                                const QJsonObject& message);
} // namespace litecode::extensions
