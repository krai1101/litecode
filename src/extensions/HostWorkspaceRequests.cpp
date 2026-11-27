#include "extensions/HostWorkspaceRequests.h"
#include "extensions/WorkspaceEditTransfer.h"
#include "extensions/TextEditorEditWire.h"
#include "extensions/JsonMessageTransfer.h"
#include <QMutexLocker>
#include <limits>
#include <stdexcept>
#include <utility>

namespace litecode::extensions {
HostWorkspaceRequests::HostWorkspaceRequests(std::function<void(const QJsonObject&)> send) : send_(std::move(send)) {}
HostWorkspaceRequests::~HostWorkspaceRequests() { shutdown(); }
QFuture<bool> HostWorkspaceRequests::applyEditorEdit(const QString& editorId, const TextEditorEditData& edit) {
    return request({{"event", "editorApplyEdit"}, {"editorId", editorId}, {"edit", wire::encodeTextEditorEdit(edit)}})
        .then([](const QJsonObject& reply) {
            if (!reply.value("applied").isBool()) throw std::runtime_error("Invalid editor edit result.");
            return reply.value("applied").toBool();
        });
}
QFuture<bool> HostWorkspaceRequests::applyEdit(const WorkspaceEditRequest& edit) {
    auto message = wire::encodeWorkspaceEdit(edit);
    message.insert("event", "workspaceApplyEdit");
    return request(std::move(message)).then([](const QJsonObject& result) {
        if (!result.value("applied").isBool()) throw std::runtime_error("Invalid workspace edit result.");
        return result.value("applied").toBool();
    });
}
namespace {
QString openedDocumentId(const QJsonObject& result) {
    const auto value = result.value("documentId");
    bool valid = false;
    const auto id = value.toString().toULongLong(&valid);
    if (!value.isString() || !valid || !id || QString::number(id) != value.toString())
        throw std::runtime_error("Invalid document opening result.");
    return value.toString();
}
}
QFuture<QString> HostWorkspaceRequests::open(const QUrl& uri, const TextDocumentOpenOptions& options) {
    QJsonObject message{{"event", "workspaceOpenDocument"}, {"uri", uri.toString(QUrl::FullyEncoded)}};
    if (options.encoding) message.insert("encoding", *options.encoding);
    return request(std::move(message)).then(openedDocumentId);
}
QFuture<QString> HostWorkspaceRequests::create(const UntitledTextDocumentOptions& options) {
    QJsonObject message{{"event", "workspaceCreateDocument"}};
    if (options.encoding) message.insert("encoding", *options.encoding);
    if (options.language) message.insert("language", *options.language);
    if (options.content) message.insert("content", *options.content);
    return request(std::move(message)).then(openedDocumentId);
}
QFuture<QJsonObject> HostWorkspaceRequests::request(QJsonObject message) {
    auto promise = std::make_shared<QPromise<QJsonObject>>();
    promise->start();
    const auto future = promise->future();
    quint64 id;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_ || nextId_ == std::numeric_limits<quint64>::max()) {
            promise->setException(std::make_exception_ptr(std::runtime_error("Workspace request service unavailable.")));
            promise->finish();
            return future;
        }
        id = ++nextId_;
        pending_.insert(id, promise);
    }
    message.insert("requestId", QString::number(id));
    QMetaObject::invokeMethod(this, [this, id, message = std::move(message)] {
        { QMutexLocker lock(&mutex_); if (stopped_ || !pending_.contains(id)) return; }
        try {
            if (message.value("event") == QJsonValue("workspaceApplyEdit")) wire::sendWorkspaceEdit(message, send_);
            else if (message.value("event") == QJsonValue("editorApplyEdit"))
                wire::sendJsonTransfer(message, {"event", "editorApplyEdit", "editorApplyEdit"}, send_);
            else send_(message);
        }
        catch (...) { complete(id, {}, "Could not send workspace request."); }
    }, Qt::QueuedConnection);
    return future;
}
QFuture<std::optional<QUrl>> HostWorkspaceRequests::save(const QUrl& uri, bool saveAs) {
    return request({{"event", "workspaceSave"}, {"uri", uri.toString(QUrl::FullyEncoded)}, {"saveAs", saveAs}})
        .then([](const QJsonObject& result) -> std::optional<QUrl> {
            const auto value = result.value("uri");
            if (value.isNull()) return {};
            if (!value.isString()) throw std::runtime_error("Invalid workspace save result.");
            const QUrl uri(value.toString());
            if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty()) throw std::runtime_error("Invalid saved resource URI.");
            return uri;
        });
}
QFuture<bool> HostWorkspaceRequests::saveAll(bool includeUntitled) {
    return request({{"event", "workspaceSaveAll"}, {"includeUntitled", includeUntitled}})
        .then([](const QJsonObject& result) {
            if (!result.value("saved").isBool()) throw std::runtime_error("Invalid workspace save-all result.");
            return result.value("saved").toBool();
        });
}
void HostWorkspaceRequests::complete(quint64 id, const QJsonObject& result, const QString& error) {
    std::shared_ptr<QPromise<QJsonObject>> promise;
    { QMutexLocker lock(&mutex_); promise = pending_.take(id); }
    if (!promise) return;
    if (error.isEmpty()) promise->addResult(result);
    else promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    promise->finish();
}
void HostWorkspaceRequests::shutdown() {
    QHash<quint64, std::shared_ptr<QPromise<QJsonObject>>> pending;
    { QMutexLocker lock(&mutex_); if (stopped_) return; stopped_ = true; pending = std::exchange(pending_, {}); }
    for (const auto& promise : pending) {
        promise->setException(std::make_exception_ptr(std::runtime_error("Extension host stopped before workspace request completed.")));
        promise->finish();
    }
}
} // namespace litecode::extensions
