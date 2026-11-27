#pragma once
#include "extensions/TextEditorEditData.h"
#include <QJsonObject>

namespace litecode::extensions::wire {
QJsonObject encodeTextEditorEdit(const TextEditorEditData& data);
std::optional<TextEditorEditData> decodeTextEditorEdit(const QJsonObject& message);
} // namespace litecode::extensions::wire
