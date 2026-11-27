#pragma once
#include "extensions/NativeViewApi.h"
#include <QJsonObject>
#include <optional>
namespace litecode::extensions::wire {
QJsonObject encodeNativeControl(const NativeControl& control);
std::optional<NativeControl> decodeNativeControl(const QJsonObject& object);
QString validateNativeViewContributions(const QJsonObject& contributions);
} // namespace litecode::extensions::wire
