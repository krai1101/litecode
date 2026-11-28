#pragma once
#include "extensions/ProgressApi.h"
#include <QJsonObject>

namespace litecode::extensions::wire {
QJsonObject encodeProgressOptions(const ProgressOptions& options);
std::optional<ProgressOptions> decodeProgressOptions(const QJsonObject& options);
QJsonObject encodeProgressReport(const ProgressReport& report);
std::optional<ProgressReport> decodeProgressReport(const QJsonObject& report);
} // namespace litecode::extensions::wire
