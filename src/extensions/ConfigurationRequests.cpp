#include "extensions/ConfigurationRequests.h"
#include "extensions/ConfigurationService.h"

namespace litecode::extensions {

QString applyConfigurationUpdate(ConfigurationService& service, const QJsonObject& message) {
    if (!message.value("key").isString() || !message.value("remove").isBool())
        return "Invalid configuration update request.";
    const auto key = message.value("key").toString();
    const bool remove = message.value("remove").toBool();
    if (key.isEmpty() || remove == message.contains("value"))
        return "Invalid configuration update value.";
    ConfigurationResource resource;
    if (message.contains("scope")) {
        if (!message.value("scope").isObject())
            return "Invalid configuration scope.";
        const auto scope = message.value("scope").toObject();
        if (scope.contains("uri")) {
            if (!scope.value("uri").isString())
                return "Invalid configuration resource URI.";
            resource.uri = QUrl(scope.value("uri").toString(), QUrl::StrictMode);
            if (!resource.uri->isValid() || resource.uri->isRelative())
                return "Invalid configuration resource URI.";
        }
        if (scope.contains("languageId")) {
            if (!scope.value("languageId").isString() ||
                scope.value("languageId").toString().isEmpty())
                return "Invalid configuration language scope.";
            resource.languageId = scope.value("languageId").toString();
        }
    }
    std::optional<ConfigurationWriteTarget> target;
    if (message.contains("target")) {
        const auto value = message.value("target");
        if (!value.isDouble() || value.toDouble() < 1 || value.toDouble() > 3 ||
            value.toDouble() != value.toInt())
            return "Invalid configuration target.";
        target = static_cast<ConfigurationWriteTarget>(value.toInt());
    }
    std::optional<bool> overrideInLanguage;
    if (message.contains("overrideInLanguage")) {
        if (!message.value("overrideInLanguage").isBool())
            return "Invalid language override flag.";
        overrideInLanguage = message.value("overrideInLanguage").toBool();
    }
    try {
        service.update(key, remove ? QJsonValue(QJsonValue::Undefined) : message.value("value"),
                       resource, target, overrideInLanguage);
        return {};
    } catch (const std::exception& error) {
        return QString::fromUtf8(error.what());
    } catch (...) {
        return "Configuration update failed.";
    }
}

} // namespace litecode::extensions
