#include "CodexDiffDocuments.h"
#include <QCryptographicHash>
#include <stdexcept>
namespace litecode::codex {
QUrl CodexDiffDocuments::add(const QString& turn, const QString& path, bool before,
                             const QByteArray& bytes) {
    const auto key = QCryptographicHash::hash(turn.toUtf8() + (before ? "before" : "after") + bytes,
                                              QCryptographicHash::Sha256)
                         .toHex();
    QUrl uri;
    uri.setScheme("codex-diff");
    uri.setHost("snapshot");
    uri.setQuery("version=" + QString::fromLatin1(key));
    uri.setPath(path.startsWith('/') ? path : '/' + path);
    contents_.insert(uri.toString(), QString::fromUtf8(bytes));
    return uri;
}
extensions::ProviderResult<QString>
CodexDiffDocuments::provideTextDocumentContent(const QUrl& uri,
                                               extensions::CancellationToken token) {
    if (token.isCancellationRequested())
        throw std::runtime_error("Diff loading canceled.");
    const auto found = contents_.constFind(uri.toString());
    if (found == contents_.cend())
        throw std::runtime_error("This change snapshot is no longer available.");
    return found.value();
}
} // namespace litecode::codex
