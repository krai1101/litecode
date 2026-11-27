#include "extensions/ConfigurationRegistry.h"
#include "extensions/DebugAdapterContribution.h"
#include "extensions/ExtensionManager.h"
#include "extensions/NativeViewWire.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>

namespace litecode::extensions {

QString ExtensionManager::validateManifest(const QJsonObject& manifest) {
    static const QRegularExpression idPattern("^[a-zA-Z0-9_-]+\\.[a-zA-Z0-9_-]+$");
    if (!idPattern.match(manifest.value("id").toString()).hasMatch())
        return "Expected id publisher.name.";
    if (manifest.value("displayName").toString().trimmed().isEmpty())
        return "Missing displayName.";
    static const QRegularExpression versionPattern("^[0-9]+\\.[0-9]+\\.[0-9]+$");
    if (!versionPattern.match(manifest.value("version").toString()).hasMatch())
        return "Expected version major.minor.patch.";
    if (manifest.value("apiVersion").toInt() != ApiVersion)
        return QStringLiteral("Extension requires LiteCode SDK %1; this app requires SDK %2. "
                              "Install a matching extension build.")
            .arg(manifest.value("apiVersion").toInt())
            .arg(ApiVersion);
    if (manifest.value("qtVersion").toString() != QString::fromLatin1(qVersion()))
        return "Build with the same Qt version as LiteCode.";
    if (manifest.value("architecture").toString() != QSysInfo::buildCpuArchitecture())
        return "Incompatible CPU architecture.";

    QSet<QString> contributedCommands;
    QSet<QString> contributedParticipants;
    if (manifest.contains("contributes")) {
        if (!manifest.value("contributes").isObject())
            return "Expected contributes object.";
        const auto contributions = manifest.value("contributes").toObject();
        for (auto it = contributions.begin(); it != contributions.end(); ++it)
            if (it.key() != "commands" && it.key() != "chatParticipants" &&
                it.key() != "debuggers" && it.key() != "breakpoints" && it.key() != "formatters" &&
                it.key() != "configuration" && it.key() != "configurationDefaults" &&
                it.key() != "views" && it.key() != "viewsContainers")
                return "Unsupported contribution point: " + it.key();
        const auto configurationError = ConfigurationRegistry::validateContribution(contributions);
        if (!configurationError.isEmpty())
            return configurationError;
        const auto viewError = wire::validateNativeViewContributions(contributions);
        if (!viewError.isEmpty())
            return viewError;
        if (contributions.contains("chatParticipants") &&
            !contributions.value("chatParticipants").isArray())
            return "Expected contributes.chatParticipants array.";
        static const QRegularExpression participantNamePattern("^[A-Za-z0-9_-]+$");
        for (const auto& value : contributions.value("chatParticipants").toArray()) {
            if (!value.isObject())
                return "Expected chat participant contribution object.";
            const auto participant = value.toObject();
            const auto id = participant.value("id").toString();
            const auto name = participant.value("name").toString();
            if (id.trimmed().isEmpty() || id != id.trimmed() || id.contains(QChar('\0')) ||
                id.contains('\r') || id.contains('\n'))
                return "Invalid chat participant ID.";
            if (!participantNamePattern.match(name).hasMatch())
                return "Invalid chat participant name.";
            if (participant.contains("fullName") &&
                (participant.value("fullName").toString().trimmed().isEmpty() ||
                 !participant.value("fullName").isString()))
                return "Invalid chat participant fullName.";
            if (participant.contains("description") && !participant.value("description").isString())
                return "Expected chat participant description string.";
            if (participant.contains("isSticky") && !participant.value("isSticky").isBool())
                return "Expected chat participant isSticky boolean.";
            if (participant.contains("commands") && !participant.value("commands").isArray())
                return "Expected chat participant commands array.";
            QSet<QString> commandNames;
            for (const auto& commandValue : participant.value("commands").toArray()) {
                const auto command = commandValue.toObject();
                const auto commandName = command.value("name").toString();
                if (!commandValue.isObject() ||
                    !participantNamePattern.match(commandName).hasMatch() ||
                    commandNames.contains(commandName) ||
                    (command.contains("description") && !command.value("description").isString()))
                    return "Invalid chat participant command.";
                commandNames.insert(commandName);
            }
            if (participant.contains("disambiguation") &&
                !participant.value("disambiguation").isArray())
                return "Expected chat participant disambiguation array.";
            for (const auto& ruleValue : participant.value("disambiguation").toArray()) {
                const auto rule = ruleValue.toObject();
                if (!ruleValue.isObject() ||
                    rule.value("category").toString().trimmed().isEmpty() ||
                    rule.value("description").toString().trimmed().isEmpty() ||
                    !rule.value("examples").isArray())
                    return "Invalid chat participant disambiguation rule.";
                for (const auto& example : rule.value("examples").toArray())
                    if (!example.isString())
                        return "Invalid chat participant disambiguation example.";
            }
            if (contributedParticipants.contains(id))
                return "Duplicate chat participant contribution.";
            contributedParticipants.insert(id);
        }
        if (contributions.contains("commands") && !contributions.value("commands").isArray())
            return "Expected contributes.commands array.";
        for (const auto& value : contributions.value("commands").toArray()) {
            if (!value.isObject())
                return "Expected command contribution object.";
            const auto command = value.toObject();
            const auto id = command.value("command").toString();
            if (id.trimmed().isEmpty() || id != id.trimmed() || id.contains(QChar('\0')) ||
                id.contains('\n') || id.contains('\r'))
                return "Invalid contributed command ID.";
            if (command.value("title").toString().trimmed().isEmpty())
                return "Missing command title.";
            if (command.contains("category") && !command.value("category").isString())
                return "Expected command category string.";
            if (contributedCommands.contains(id))
                return "Duplicate command contribution.";
            contributedCommands.insert(id);
        }
        if (contributions.contains("breakpoints") && !contributions.value("breakpoints").isArray())
            return "Expected contributes.breakpoints array.";
        QSet<QString> breakpointLanguages;
        for (const auto& value : contributions.value("breakpoints").toArray()) {
            const auto language = value.toObject().value("language").toString();
            if (!value.isObject() || language.trimmed().isEmpty() || language.contains(QRegularExpression("\\s")) ||
                breakpointLanguages.contains(language))
                return "Breakpoint languages must be nonempty and unique.";
            breakpointLanguages.insert(language);
        }
        if (contributions.contains("debuggers") && !contributions.value("debuggers").isArray())
            return "Expected contributes.debuggers array.";
        if (contributions.contains("formatters") && !contributions.value("formatters").isArray())
            return "Expected contributes.formatters array.";
        QSet<QString> formatterLanguages;
        for (const auto& value : contributions.value("formatters").toArray()) {
            if (!value.isObject())
                return "Expected formatter contribution object.";
            const auto language = value.toObject().value("language").toString();
            if (language.trimmed().isEmpty() || language != language.trimmed() ||
                language.contains(QRegularExpression("\\s")) ||
                formatterLanguages.contains(language))
                return "Formatter language must be nonempty and unique.";
            formatterLanguages.insert(language);
        }
        QSet<QString> debuggerTypes;
        for (const auto& value : contributions.value("debuggers").toArray()) {
            if (!value.isObject())
                return "Expected debugger contribution object.";
            const auto debugger = value.toObject();
            const QString type = debugger.value("type").toString();
            if (type.trimmed().isEmpty() || debuggerTypes.contains(type))
                return "Debugger type must be nonempty and unique.";
            debuggerTypes.insert(type);
            if (const auto error = validateDebugAdapterContribution(debugger); !error.isEmpty())
                return error;
            if (debugger.contains("label") && !debugger.value("label").isString())
                return "Expected debugger label string.";
            if (debugger.contains("initialConfigurations")) {
                if (!debugger.value("initialConfigurations").isArray())
                    return "Expected debugger initialConfigurations array.";
                for (const auto& configuration : debugger.value("initialConfigurations").toArray())
                    if (!configuration.isObject())
                        return "Expected initial debug configuration object.";
            }
            if (debugger.contains("configurationAttributes") &&
                !debugger.value("configurationAttributes").isObject())
                return "Expected debugger configurationAttributes object.";
            const auto attributes = debugger.value("configurationAttributes").toObject();
            for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                if ((it.key() != "launch" && it.key() != "attach") || !it.value().isObject())
                    return "Expected launch or attach debugger schema.";
            }
        }
    }

    if (manifest.contains("activationEvents") && !manifest.value("activationEvents").isArray())
        return "Expected activationEvents array.";
    const auto events = manifest.value("activationEvents").toArray();
    if (events.isEmpty() && contributedCommands.isEmpty() && contributedParticipants.isEmpty() &&
        manifest.value("contributes").toObject().value("debuggers").toArray().isEmpty() &&
        manifest.value("contributes").toObject().value("breakpoints").toArray().isEmpty() &&
        manifest.value("contributes").toObject().value("formatters").toArray().isEmpty() &&
        !manifest.value("contributes").toObject().contains("configuration") &&
        !manifest.value("contributes").toObject().contains("configurationDefaults") &&
        !manifest.value("contributes").toObject().contains("views"))
        return "Provide activationEvents or contributions.";
    for (const auto& event : events) {
        const auto value = event.toString();
        const bool languageEvent =
            value == "onLanguage" || (value.startsWith("onLanguage:") && value.size() > 11 &&
                                      !value.mid(11).contains(QRegularExpression("\\s")));
        const bool commandEvent = value.startsWith("onCommand:") && value.size() > 10 &&
                                  value.mid(10) == value.mid(10).trimmed() &&
                                  !value.contains(QChar('\0')) && !value.contains('\r') &&
                                  !value.contains('\n');
        const bool participantEvent = value.startsWith("onChatParticipant:") && value.size() > 18 &&
                                      value.mid(18) == value.mid(18).trimmed() &&
                                      !value.contains(QChar('\0')) && !value.contains('\r') &&
                                      !value.contains('\n');
        const bool debugEvent =
            value == "onDebug" || value == "onDebugInitialConfigurations" ||
            value == "onDebugDynamicConfigurations" ||
            (value.startsWith("onDebugResolve:") && value.size() > 15 &&
             !value.contains(QRegularExpression("\\s")) && !value.contains(QChar('\0')));
        if (value != "onStartupFinished" && value != "onUri" && !languageEvent && !commandEvent &&
            !participantEvent && !debugEvent &&
            !(value.startsWith("onView:") && value.size() > 7 &&
              !value.contains(QRegularExpression("\\s"))))
            return "Unsupported activation event.";
    }
    return {};
}

} // namespace litecode::extensions
