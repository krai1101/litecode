#pragma once
#include "extensions/DebugApi.h"
#include <QObject>
#include <QHash>
#include <QSet>
#include <QMap>
#include <functional>

namespace litecode::extensions {
class HostDebugTrackers final : public QObject {
  public:
    HostDebugTrackers(std::function<void(const QJsonObject&)> send, QObject* parent = nullptr);
    ~HostDebugTrackers() override;
    Disposable registerDebugAdapterTrackerFactory(const QString& type, std::shared_ptr<DebugAdapterTrackerFactory> factory);
    void prepare(quint64 id, std::shared_ptr<DebugSession> session);
    bool event(quint64 id, const QJsonObject& message);
    void release(quint64 id);
    void shutdown();
  private:
    struct Factory { QString type; std::shared_ptr<DebugAdapterTrackerFactory> provider; };
    struct Group {
        QMap<quint64, std::shared_ptr<DebugAdapterTracker>> trackers;
        QSet<quint64> awaiting;
        bool ready{}, started{}, stopping{};
    };
    void complete(quint64 id, quint64 handle, std::shared_ptr<DebugAdapterTracker> tracker);
    void ready(quint64 id, bool timeout = false);
    template<typename T> void watch(quint64 id, quint64 handle, const QFuture<T>& future,
        std::shared_ptr<DebugAdapterTrackerFactory> factory);
    std::function<void(const QJsonObject&)> send_;
    QHash<quint64, Factory> factories_;
    QHash<quint64, Group> groups_;
    quint64 nextHandle_{};
    bool stopped_{};
};
} // namespace litecode::extensions
