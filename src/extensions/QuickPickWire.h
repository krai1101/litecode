#pragma once
#include "extensions/QuickPickApi.h"
#include <QJsonObject>
#include <QJsonArray>
namespace litecode::extensions::wire {
QJsonObject encodeQuickPickOptions(const QuickPickOptions& options);
std::optional<QuickPickOptions> decodeQuickPickOptions(const QJsonObject& options);
QJsonArray encodeQuickPickItems(const QVector<QuickPickItem>& items);
std::optional<QVector<QuickPickItem>> decodeQuickPickItems(const QJsonArray& items);
}
