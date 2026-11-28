#include "CodexThreadLink.h"
#include <QRegularExpression>
#include <stdexcept>
namespace litecode::codex {
namespace {
bool validThread(const QString& id) {
    static const QRegularExpression pattern("^[A-Za-z0-9_-]{1,128}$");
    return pattern.match(id).hasMatch();
}
} // namespace
QUrl codexThreadLink(const QString& scheme, const QString& extensionId, const QString& threadId) {
    if (scheme.isEmpty() || extensionId.isEmpty() || !validThread(threadId))
        throw std::invalid_argument("Invalid Codex chat link.");
    QUrl result;
    result.setScheme(scheme);
    result.setHost(extensionId);
    result.setPath("/thread/" + threadId);
    return result;
}
CodexThreadLinkHandler::CodexThreadLinkHandler(QString scheme, QString extensionId,
                                              std::function<void(QString)> open)
    : scheme_(std::move(scheme)), extensionId_(std::move(extensionId)), open_(std::move(open)) {}
extensions::ProviderResult<void> CodexThreadLinkHandler::handleUri(const QUrl& uri) {
    const auto parts = uri.path().split('/', Qt::SkipEmptyParts);
    if (uri.scheme().compare(scheme_, Qt::CaseInsensitive) ||
        uri.host().compare(extensionId_, Qt::CaseInsensitive) || !uri.userInfo().isEmpty() ||
        uri.port() != -1 || uri.hasQuery() || uri.hasFragment() || parts.size() != 2 ||
        parts[0] != "thread" || !validThread(parts[1]))
        throw std::invalid_argument("Invalid Codex chat link.");
    open_(parts[1]);
    return std::monostate{};
}
} // namespace litecode::codex
