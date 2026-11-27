#pragma once
#include "extensions/MarkdownApi.h"
#include <QJsonObject>

namespace litecode::extensions::wire {
QJsonObject encodeMarkdownString(const MarkdownString& value);
std::optional<MarkdownString> decodeMarkdownString(const QJsonObject& value);
} // namespace litecode::extensions::wire
