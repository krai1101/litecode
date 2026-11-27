#pragma once
#include "extensions/ThemeApi.h"
#include <QJsonValue>

namespace litecode::extensions::wire {
QJsonValue encodeIconPath(const IconPath& icon);
std::optional<IconPath> decodeIconPath(const QJsonValue& icon);
} // namespace litecode::extensions::wire
