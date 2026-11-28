#pragma once
#include "extensions/DialogApi.h"
#include <QJsonObject>

namespace litecode::extensions::wire {
QJsonObject encodeOpenDialogOptions(const OpenDialogOptions& options);
QJsonObject encodeSaveDialogOptions(const SaveDialogOptions& options);
std::optional<OpenDialogOptions> decodeOpenDialogOptions(const QJsonObject& options);
std::optional<SaveDialogOptions> decodeSaveDialogOptions(const QJsonObject& options);
} // namespace litecode::extensions::wire
