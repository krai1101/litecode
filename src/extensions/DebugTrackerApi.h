#pragma once
#include "extensions/ProviderApi.h"
#include <QJsonObject>
#include <QString>
#include <exception>
#include <memory>
#include <optional>

namespace litecode::extensions {
class DebugSession;
class DebugAdapterTracker {
  public:
    virtual ~DebugAdapterTracker() = default;
    virtual void onWillStartSession() {}
    virtual void onWillReceiveMessage(const QJsonObject&) {}
    virtual void onDidSendMessage(const QJsonObject&) {}
    virtual void onWillStopSession() {}
    virtual void onError(const std::exception&) {}
    virtual void onExit(std::optional<int>, std::optional<QString>) {}
};
class DebugAdapterTrackerFactory {
  public:
    virtual ~DebugAdapterTrackerFactory() = default;
    virtual ProviderResult<std::shared_ptr<DebugAdapterTracker>> createDebugAdapterTracker(
        std::shared_ptr<DebugSession> session) = 0;
};
} // namespace litecode::extensions
