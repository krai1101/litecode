#pragma once
#include <QJsonValue>
#include <QUrl>
#include <optional>
namespace litecode::extensions::wire {
inline const QString AppUriScheme = QStringLiteral("litecode");
QString encodeUri(const QUrl& uri);
std::optional<QUrl> decodeUri(const QJsonValue& value);
bool isExtensionUri(const QUrl& uri, const QString& owner);
}
