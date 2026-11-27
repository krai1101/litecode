#pragma once
#include "extensions/EnvironmentApi.h"
#include <QDateTime>
#include <QJsonObject>

namespace litecode::extensions {
// Private editor-owned initialization data; not an extension API options object.
struct EnvironmentInfo final {
    QString appName, appRoot, appHost, language, machineId, sessionId;
    QDateTime firstSessionDate;
    bool isAppPortable{};
    std::optional<QString> remoteName;
    UIKind uiKind{UIKind::Desktop};
    QString shell;
    LogLevel logLevel{LogLevel::Info};
    QString uriScheme{QStringLiteral("litecode")};
};
bool isNewAppInstall(const QDateTime& firstSessionDate, const QDateTime& now);
namespace wire {
QJsonObject encodeEnvironmentInfo(const EnvironmentInfo& info);
std::optional<EnvironmentInfo> decodeEnvironmentInfo(const QJsonObject& object);
} // namespace wire
} // namespace litecode::extensions
