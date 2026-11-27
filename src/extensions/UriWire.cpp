#include "extensions/UriWire.h"
namespace litecode::extensions::wire {
QString encodeUri(const QUrl& uri) { return uri.toString(QUrl::FullyEncoded); }
std::optional<QUrl> decodeUri(const QJsonValue& value) {
    if (!value.isString())
        return {};
    const auto text = value.toString();
    QUrl uri(text, QUrl::StrictMode);
    if (!uri.isValid() || uri.scheme().trimmed().isEmpty())
        return {};
    return uri;
}
bool isExtensionUri(const QUrl& uri, const QString& owner) {
    // Authority must be exactly an extension ID, never userinfo or a port.
    return uri.isValid() && uri.scheme().compare(AppUriScheme, Qt::CaseInsensitive) == 0 &&
           uri.authority(QUrl::FullyEncoded).compare(owner, Qt::CaseInsensitive) == 0 &&
           !owner.isEmpty();
}
} // namespace litecode::extensions::wire
