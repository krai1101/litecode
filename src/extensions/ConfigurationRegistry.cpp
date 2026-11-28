#include "extensions/ConfigurationRegistry.h"

#include <QJsonArray>
#include <QSet>
#include <functional>

namespace litecode::extensions {
namespace {

SettingScope scope(const QString& value) {
    if (value == "application")
        return SettingScope::Application;
    if (value == "machine")
        return SettingScope::Machine;
    if (value == "resource")
        return SettingScope::Resource;
    if (value == "language-overridable")
        return SettingScope::LanguageOverridable;
    if (value == "machine-overridable")
        return SettingScope::MachineOverridable;
    return SettingScope::Window;
}

QJsonValue typeDefault(const QJsonValue& type) {
    const auto name = type.isArray() && !type.toArray().isEmpty()
                          ? type.toArray().first().toString()
                          : type.toString();
    if (name == "boolean")
        return false;
    if (name == "integer" || name == "number")
        return 0;
    if (name == "string")
        return QString{};
    if (name == "array")
        return QJsonArray{};
    if (name == "object")
        return QJsonObject{};
    return QJsonValue::Null;
}

QJsonValue mergeDefault(const QJsonValue& lower, const QJsonValue& higher) {
    if (!lower.isObject() || !higher.isObject())
        return higher;
    auto result = lower.toObject();
    const auto incoming = higher.toObject();
    for (auto it = incoming.begin(); it != incoming.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

QVector<QJsonObject> groups(const QJsonValue& configuration) {
    QVector<QJsonObject> result;
    if (configuration.isObject())
        result.append(configuration.toObject());
    else
        for (const auto& group : configuration.toArray())
            result.append(group.toObject());
    return result;
}

QString validateGroup(const QJsonObject& group, int depth) {
    if (depth > 32)
        return "Configuration contribution nesting is too deep.";
    if (group.contains("properties") && !group.value("properties").isObject())
        return "Expected configuration properties object.";
    const auto properties = group.value("properties").toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (it.key().trimmed().isEmpty() || it.key() != it.key().trimmed() ||
            it.key().split('.').contains(QString{}) || it.key().contains(QChar('\0')))
            return "Invalid configuration property key: " + it.key();
        if (!it.value().isObject())
            return "Expected configuration property schema: " + it.key();
        const auto schema = it.value().toObject();
        if (schema.contains("scope") && !schema.value("scope").isString())
            return "Expected configuration property scope string: " + it.key();
        const auto type = schema.value("type");
        if (!type.isUndefined() && !type.isString() && !type.isArray())
            return "Expected configuration property type string or array: " + it.key();
        const auto types = type.isArray() ? type.toArray() : QJsonArray{type};
        static const QSet<QString> validTypes{"boolean", "integer", "number", "string",
                                              "array",   "object",  "null"};
        if (!type.isUndefined()) {
            if (types.isEmpty())
                return "Configuration property type array is empty: " + it.key();
            for (const auto& candidate : types)
                if (!candidate.isString() || !validTypes.contains(candidate.toString()))
                    return "Invalid configuration property type: " + it.key();
        }
    }
    if (group.contains("allOf")) {
        if (!group.value("allOf").isArray())
            return "Expected configuration allOf array.";
        for (const auto& child : group.value("allOf").toArray()) {
            if (!child.isObject())
                return "Expected configuration allOf group object.";
            const auto error = validateGroup(child.toObject(), depth + 1);
            if (!error.isEmpty())
                return error;
        }
    }
    return {};
}

bool permitsDefault(const ConfigurationProperty& property) {
    return !property.schema.value("disallowConfigurationDefault").toBool() &&
           property.scope != SettingScope::Application && property.scope != SettingScope::Machine;
}

} // namespace

QString ConfigurationRegistry::validateContribution(const QJsonObject& contributes) {
    const auto configuration = contributes.value("configuration");
    if (!configuration.isUndefined()) {
        if (!configuration.isObject() && !configuration.isArray())
            return "Expected contributes.configuration object or array.";
        if (configuration.isArray())
            for (const auto& group : configuration.toArray())
                if (!group.isObject())
                    return "Expected configuration group object.";
        for (const auto& group : groups(configuration)) {
            const auto error = validateGroup(group, 0);
            if (!error.isEmpty())
                return error;
        }
    }
    const auto defaults = contributes.value("configurationDefaults");
    if (!defaults.isUndefined() && !defaults.isObject())
        return "Expected contributes.configurationDefaults object.";
    return {};
}

void ConfigurationRegistry::replace(const QVector<ConfigurationContribution>& contributions) {
    ConfigurationRegistry next;
    for (const auto& contribution : contributions) {
        const auto error = validateContribution(contribution.contributes);
        if (!error.isEmpty()) {
            next.diagnostics_[contribution.extensionId].append(error);
            continue;
        }
        next.contributions_.append(contribution);
        std::function<void(const QJsonObject&, SettingScope)> visit;
        visit = [&](const QJsonObject& group, SettingScope inherited) {
            if (group.contains("scope"))
                inherited = scope(group.value("scope").toString());
            const auto properties = group.value("properties").toObject();
            for (auto it = properties.begin(); it != properties.end(); ++it) {
                const auto schema = it.value().toObject();
                if (!configurationOverrideIdentifiers(it.key()).isEmpty()) {
                    next.diagnostics_[contribution.extensionId].append(
                        "Use configurationDefaults for language override property " + it.key());
                    continue;
                }
                if (next.properties_.contains(it.key())) {
                    next.diagnostics_[contribution.extensionId].append(
                        "Configuration property is already registered: " + it.key());
                    continue;
                }
                if (schema.contains("included") && !schema.value("included").toBool())
                    continue;
                const auto declaredScope =
                    schema.contains("scope") ? scope(schema.value("scope").toString()) : inherited;
                const auto declaredDefault = schema.contains("default")
                                                 ? schema.value("default")
                                                 : typeDefault(schema.value("type"));
                next.properties_.insert(it.key(), {it.key(), contribution.extensionId, schema,
                                                   declaredScope, declaredDefault});
            }
            for (const auto& child : group.value("allOf").toArray())
                visit(child.toObject(), inherited);
        };
        for (const auto& group : groups(contribution.contributes.value("configuration")))
            visit(group, SettingScope::Window);
    }
    // Record rejected defaults independently of activation/runtime errors. A
    // bad setting declaration must not prevent unrelated commands from running.
    for (const auto& contribution : next.contributions_) {
        const auto overrides = contribution.contributes.value("configurationDefaults").toObject();
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            if (!configurationOverrideIdentifiers(it.key()).isEmpty()) {
                if (!it.value().isObject())
                    next.diagnostics_[contribution.extensionId].append(
                        "Language configuration default must be an object: " + it.key());
                continue;
            }
            const auto property = next.properties_.constFind(it.key());
            if (property != next.properties_.cend() && !permitsDefault(*property))
                next.diagnostics_[contribution.extensionId].append(
                    "Configuration default is not allowed for " + it.key());
        }
    }
    *this = std::move(next);
}

ConfigurationLayer ConfigurationRegistry::defaults() const {
    QMap<QString, QJsonValue> values;
    for (auto it = properties_.begin(); it != properties_.end(); ++it)
        values.insert(it.key(), it->defaultValue);
    QVector<std::pair<QStringList, QJsonObject>> languageGroups;
    for (const auto& contribution : contributions_) {
        const auto overrides = contribution.contributes.value("configurationDefaults").toObject();
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            const auto languages = configurationOverrideIdentifiers(it.key());
            if (!languages.isEmpty()) {
                if (!it.value().isObject())
                    continue;
                QJsonObject accepted;
                const auto settings = it.value().toObject();
                for (auto setting = settings.begin(); setting != settings.end(); ++setting) {
                    const auto property = properties_.constFind(setting.key());
                    if (property != properties_.cend() && permitsDefault(*property) &&
                        property->scope == SettingScope::LanguageOverridable)
                        accepted.insert(setting.key(), setting.value());
                }
                // Registry-level default contributions use a shallow merge
                // per setting. The effective configuration model subsequently
                // performs its separate recursive layer merge.
                bool found = false;
                for (auto& group : languageGroups) {
                    if (group.first != languages)
                        continue;
                    for (auto setting = accepted.begin(); setting != accepted.end(); ++setting)
                        group.second.insert(
                            setting.key(),
                            mergeDefault(group.second.value(setting.key()), setting.value()));
                    found = true;
                    break;
                }
                if (!found)
                    languageGroups.append({languages, accepted});
            } else {
                const auto property = properties_.constFind(it.key());
                if (property != properties_.cend() && permitsDefault(*property)) {
                    const auto updated = property->schema.value("type") == QJsonValue("object")
                                             ? mergeDefault(values.value(it.key()), it.value())
                                             : it.value();
                    values.insert(it.key(), updated);
                }
            }
        }
    }
    ConfigurationLayer result;
    for (auto it = values.begin(); it != values.end(); ++it)
        result.addSetting(it.key(), it.value());
    for (const auto& [languages, settings] : languageGroups)
        result.addOverride(languages, settings);
    return result;
}

QStringList ConfigurationRegistry::diagnostics(const QString& extensionId) const {
    return diagnostics_.value(extensionId);
}

} // namespace litecode::extensions
