#include "extensions/EnvironmentInfo.h"
#include <QRegularExpression>

namespace litecode::extensions {
bool isNewAppInstall(const QDateTime& firstSessionDate, const QDateTime& now) {
    // The pinned implementation treats future dates as new too, and evaluates
    // on access rather than freezing a boolean for a long-running session.
    return firstSessionDate.isValid() && now.isValid() && firstSessionDate.msecsTo(now) < 24LL * 60 * 60 * 1000;
}
namespace wire {
QJsonObject encodeEnvironmentInfo(const EnvironmentInfo& info) {
    QJsonObject result{{"appName", info.appName}, {"appRoot", info.appRoot}, {"appHost", info.appHost},
        {"language", info.language}, {"machineId", info.machineId}, {"sessionId", info.sessionId},
        {"firstSessionDate", info.firstSessionDate.toUTC().toString(Qt::ISODateWithMs)},
        {"isAppPortable", info.isAppPortable}, {"uiKind", static_cast<int>(info.uiKind)}, {"shell", info.shell}, {"logLevel", static_cast<int>(info.logLevel)}, {"uriScheme", info.uriScheme}};
    if (info.remoteName) result.insert("remoteName", *info.remoteName);
    return result;
}
std::optional<EnvironmentInfo> decodeEnvironmentInfo(const QJsonObject& object) {
    for (const auto* key : {"appName", "appRoot", "appHost", "language", "machineId", "sessionId", "firstSessionDate", "shell", "uriScheme"})
        if (!object.value(key).isString() || object.value(key).toString().size() > 65536) return {};
    const auto logLevel = object.value("logLevel");
    if (!logLevel.isDouble() || logLevel.toDouble() != logLevel.toInt(-1) || logLevel.toInt(-1) < 0 || logLevel.toInt() > 5) return {};
    if (!object.value("isAppPortable").isBool() || !object.value("uiKind").isDouble() ||
        (object.value("uiKind").toDouble() != 1 && object.value("uiKind").toDouble() != 2) ||
        (object.contains("remoteName") && (!object.value("remoteName").isString() || object.value("remoteName").toString().isEmpty() ||
            object.value("remoteName").toString().size() > 65536))) return {};
    const auto first = QDateTime::fromString(object.value("firstSessionDate").toString(), Qt::ISODateWithMs);
    static const QRegularExpression schemePattern("^[a-zA-Z][a-zA-Z0-9+.-]*$");
    if (!first.isValid() || object.value("machineId").toString().isEmpty() || object.value("sessionId").toString().isEmpty() ||
        !schemePattern.match(object.value("uriScheme").toString()).hasMatch()) return {};
    return EnvironmentInfo{object.value("appName").toString(), object.value("appRoot").toString(), object.value("appHost").toString(),
        object.value("language").toString(), object.value("machineId").toString(), object.value("sessionId").toString(), first,
        object.value("isAppPortable").toBool(), object.contains("remoteName") ? std::optional(object.value("remoteName").toString()) : std::nullopt,
        static_cast<UIKind>(object.value("uiKind").toInt()), object.value("shell").toString(), static_cast<LogLevel>(logLevel.toInt()), object.value("uriScheme").toString()};
}
} // namespace wire
} // namespace litecode::extensions
