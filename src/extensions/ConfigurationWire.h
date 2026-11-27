#pragma once

#include "extensions/ConfigurationModel.h"

namespace litecode::extensions::wire {
[[nodiscard]] QJsonObject encodeConfigurationModel(const ConfigurationModel& model);
[[nodiscard]] std::optional<ConfigurationModel> decodeConfigurationModel(
    const QJsonObject& message, QString* error = nullptr);
} // namespace litecode::extensions::wire
