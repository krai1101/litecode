#pragma once
#include "extensions/WorkspaceEditApi.h"
#include <QJsonObject>
#include <QHash>
namespace litecode::extensions {
namespace detail {
class WorkspaceEditAccess final {
  public:
    static const QVector<WorkspaceEditEntry>& entries(const WorkspaceEdit& edit) { return edit.entries_; }
};
}
struct WorkspaceEditRequest final {
    QVector<detail::WorkspaceEditEntry> entries;
    QHash<QString, qint64> versions;
    std::optional<WorkspaceEditMetadata> metadata;
};
namespace wire {
QJsonObject encodeWorkspaceEdit(const WorkspaceEditRequest& request);
std::optional<WorkspaceEditRequest> decodeWorkspaceEdit(const QJsonObject& message);
}
}
