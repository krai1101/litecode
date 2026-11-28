#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <atomic>

namespace litecode::codex {
// A separate app-server can finalize its history projection as interrupted at
// EOF while the owning process is still writing. Only durable lifecycle events
// can disambiguate that snapshot; missing timing alone is not evidence of activity.
struct ObservedTurnLifecycle {
    QString path, turnId, status;
    QJsonArray activeTools;
    qint64 fileSize{-1}, modifiedMs{}, startedMs{}, completedMs{};
};
ObservedTurnLifecycle readObservedTurnLifecycle(const QString& path, const QString& turnId,
                                              const ObservedTurnLifecycle& cached = {},
                                              const std::atomic_bool* cancelled = nullptr);
QJsonObject reconcileObservedTurn(QJsonObject turn, const ObservedTurnLifecycle& lifecycle);
} // namespace litecode::codex
