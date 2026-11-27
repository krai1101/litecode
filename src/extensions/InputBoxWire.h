#pragma once
#include "extensions/InputBoxApi.h"
#include <QJsonObject>
namespace litecode::extensions::wire {
QJsonObject encodeInputBoxOptions(const InputBoxOptions& options);
std::optional<InputBoxOptions> decodeInputBoxOptions(const QJsonObject& options);
QJsonValue encodeInputBoxValidation(const InputBoxValidationResult& result);
std::optional<InputBoxValidationResult> decodeInputBoxValidation(const QJsonValue& result);
} // namespace litecode::extensions::wire
