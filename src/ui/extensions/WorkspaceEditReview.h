#pragma once

#include <QByteArray>
#include <QStringList>
#include <QUrl>

namespace litecode::ui {
// Workbench-only snapshots. A preview never binds to or modifies the live model.
struct WorkspaceEditReviewDocument final {
    QUrl uri;
    QByteArray before, after;
    QStringList annotations;
};
} // namespace litecode::ui
