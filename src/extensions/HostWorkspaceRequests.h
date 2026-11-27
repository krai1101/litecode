#pragma once
#include "extensions/DocumentOpenApi.h"
#include "extensions/WorkspaceEditWire.h"
#include "extensions/TextEditorEditData.h"
#include <QObject>
#include <QJsonObject>
#include <QFuture>
#include <QPromise>
#include <QHash>
#include <QMutex>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

namespace litecode::extensions {
// Private workspace RPC. JSON is a transport detail, not an extension API.
class HostWorkspaceRequests final : public QObject {
  public:
    explicit HostWorkspaceRequests(std::function<void(const QJsonObject&)> send);
    ~HostWorkspaceRequests() override;
    QFuture<std::optional<QUrl>> save(const QUrl& uri, bool saveAs);
    QFuture<bool> saveAll(bool includeUntitled);
    QFuture<bool> applyEdit(const WorkspaceEditRequest& edit);
    QFuture<bool> applyEditorEdit(const QString& editorId, const TextEditorEditData& edit);
    QFuture<QString> open(const QUrl& uri, const TextDocumentOpenOptions& options);
    QFuture<QString> create(const UntitledTextDocumentOptions& options);
    void complete(quint64 id, const QJsonObject& result, const QString& error);
    void shutdown();
  private:
    QFuture<QJsonObject> request(QJsonObject message);
    std::function<void(const QJsonObject&)> send_;
    QMutex mutex_;
    QHash<quint64, std::shared_ptr<QPromise<QJsonObject>>> pending_;
    quint64 nextId_{};
    bool stopped_{};
};
} // namespace litecode::extensions
