#include "debug/DebugConfiguration.h"
#include "core/JsoncDocument.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

namespace litecode::debug {
static constexpr qsizetype MaxConfigurationBytes = 1024 * 1024;
QJsonObject DebugConfigurations::parseJsonc(const QByteArray& bytes, QString* error) {
    if (error)
        error->clear();
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QJsonObject{};
    };
    if (bytes.size() > MaxConfigurationBytes)
        return fail("Configuration exceeds the 1 MiB size limit.");
    try {
        const auto document = core::JsoncDocument::parse(bytes).value();
        if (!document.isObject())
            return fail("Expected a configuration object.");
        return document.toObject();
    } catch (const std::exception& exception) {
        return fail(QString::fromUtf8(exception.what()));
    }
}

namespace {
bool schemaTypeMatches(const QJsonValue& value, const QString& type) {
    if (type == QStringLiteral("string"))
        return value.isString();
    if (type == QStringLiteral("boolean"))
        return value.isBool();
    if (type == QStringLiteral("object"))
        return value.isObject();
    if (type == QStringLiteral("array"))
        return value.isArray();
    if (type == QStringLiteral("number"))
        return value.isDouble();
    if (type == QStringLiteral("integer"))
        return value.isDouble() && value.toDouble() == double(value.toInteger());
    if (type == QStringLiteral("null"))
        return value.isNull();
    return false;
}

bool validateSchemaValue(const QJsonValue& value, const QJsonObject& schema, const QString& path,
                         QString* error, int depth) {
    const auto fail = [error, &path](const QString& problem) {
        if (error)
            *error = path + QStringLiteral(": ") + problem;
        return false;
    };
    if (depth > 64)
        return fail(QStringLiteral("schema nesting is too deep"));
    const QJsonValue type = schema.value(QStringLiteral("type"));
    if (!type.isUndefined()) {
        QStringList types;
        if (type.isString())
            types.append(type.toString());
        else if (type.isArray()) {
            for (const auto& candidate : type.toArray()) {
                if (!candidate.isString())
                    return fail(QStringLiteral("invalid schema type"));
                types.append(candidate.toString());
            }
        } else
            return fail(QStringLiteral("invalid schema type"));
        if (types.isEmpty())
            return fail(QStringLiteral("invalid schema type"));
        bool matched = false;
        for (const QString& candidate : types)
            matched |= schemaTypeMatches(value, candidate);
        if (!matched)
            return fail(QStringLiteral("expected %1").arg(types.join(QStringLiteral(" or "))));
    }
    const QJsonValue allowed = schema.value(QStringLiteral("enum"));
    if (allowed.isArray() && !allowed.toArray().contains(value))
        return fail(QStringLiteral("value is not allowed"));
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        for (const auto& required : schema.value(QStringLiteral("required")).toArray()) {
            if (!required.isString())
                return fail(QStringLiteral("invalid required schema"));
            if (!object.contains(required.toString()))
                return fail(QStringLiteral("missing %1").arg(required.toString()));
        }
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (properties.value(it.key()).isObject()) {
                if (!validateSchemaValue(it.value(), properties.value(it.key()).toObject(),
                                         path + QLatin1Char('.') + it.key(), error, depth + 1))
                    return false;
            } else if (schema.value(QStringLiteral("additionalProperties")) == false) {
                return fail(QStringLiteral("unexpected %1").arg(it.key()));
            }
        }
    }
    if (value.isArray() && schema.value(QStringLiteral("items")).isObject()) {
        const QJsonObject items = schema.value(QStringLiteral("items")).toObject();
        const QJsonArray values = value.toArray();
        for (qsizetype index = 0; index < values.size(); ++index)
            if (!validateSchemaValue(values.at(index), items,
                                     path + QStringLiteral("[%1]").arg(index), error, depth + 1))
                return false;
    }
    return true;
}
} // namespace

bool DebugConfigurations::validateAgainstSchema(const QJsonObject& configuration,
                                                const QJsonObject& schema, QString* error) {
    if (error)
        error->clear();
    return validateSchemaValue(configuration, schema, QStringLiteral("Configuration"), error, 0);
}
QString DebugConfigurations::filePath(const QString& workspaceFolder) {
    return QDir(workspaceFolder).filePath(".vscode/launch.json");
}
QVector<DebugConfiguration> DebugConfigurations::load(const QString& workspaceFolder,
                                                      QString* error) {
    if (error)
        error->clear();
    if (workspaceFolder.isEmpty())
        return {};
    QString path = filePath(workspaceFolder);
    if (!QFileInfo::exists(path))
        path = QDir(workspaceFolder).filePath(".litecode/launch.json");
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    return parse(file.read(MaxConfigurationBytes + 1), error);
}
QVector<DebugConfiguration> DebugConfigurations::parse(const QByteArray& jsonc, QString* error,
                                                       const QString& platform) {
    QString problem;
    const auto root = parseJsonc(jsonc, &problem);
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return QVector<DebugConfiguration>{};
    };
    if (error)
        error->clear();
    if (!problem.isEmpty())
        return fail(problem);
    if (root.contains("version") && root.value("version").toString() != "0.2.0")
        return fail("Unsupported launch configuration version.");
    if (!root.value("configurations").isArray())
        return fail("Expected configurations array.");
    QString target = platform;
    if (target.isEmpty()) {
#ifdef Q_OS_WIN
        target = "windows";
#elif defined(Q_OS_MACOS)
        target = "osx";
#else
        target = "linux";
#endif
    }
    QVector<DebugConfiguration> result;
    for (const auto& value : root.value("configurations").toArray()) {
        if (!value.isObject())
            return fail("Expected each configuration to be an object.");
        auto configuration = value.toObject();
        for (const auto& key : {"windows", "linux", "osx"}) {
            if (configuration.contains(key) && !configuration.value(key).isObject())
                return fail("Expected platform override object.");
        }
        const auto overrides = configuration.value(target).toObject();
        if (overrides.contains("type"))
            return fail("Debugger type must not be platform-specific.");
        for (auto it = overrides.begin(); it != overrides.end(); ++it)
            configuration.insert(it.key(), it.value());
        configuration.remove("windows");
        configuration.remove("linux");
        configuration.remove("osx");
        const auto name = configuration.value("name").toString();
        const auto type = configuration.value("type").toString();
        const auto request = configuration.value("request").toString();
        if (name.trimmed().isEmpty() || type.trimmed().isEmpty() ||
            (request != "launch" && request != "attach"))
            return fail("Each configuration requires name, type and launch/attach request.");
        // All other fields belong to the contributed debugger schema. Adapter
        // execution is supplied separately by its descriptor factory.
        result.append({name, type, request, configuration});
    }
    return result;
}
static QJsonValue resolveValue(const QJsonValue& value, const VariableContext& context,
                               QString& error, int depth) {
    if (depth > 64) {
        error = "Configuration nesting is too deep.";
        return {};
    }
    if (value.isObject()) {
        QJsonObject output;
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            output.insert(it.key(), resolveValue(it.value(), context, error, depth + 1));
            if (!error.isEmpty())
                return {};
        }
        return output;
    }
    if (value.isArray()) {
        QJsonArray output;
        for (const auto& element : value.toArray()) {
            output.append(resolveValue(element, context, error, depth + 1));
            if (!error.isEmpty())
                return {};
        }
        return output;
    }
    if (!value.isString())
        return value;
    const auto text = value.toString();
    static const QRegularExpression pattern("\\$\\{([^{}]+)\\}");
    auto matches = pattern.globalMatch(text);
    QString output;
    qsizetype offset = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        const auto key = match.captured(1);
        const auto prefix = text.mid(offset, match.capturedStart() - offset);
        if (prefix.contains("${")) {
            error = "Malformed substitution variable.";
            return {};
        }
        output += prefix;
        QString replacement;
        if (key == "workspaceFolder")
            replacement = context.workspaceFolder;
        else if (key == "workspaceFolderBasename")
            replacement = QFileInfo(context.workspaceFolder).fileName();
        else if (key == "file")
            replacement = context.file;
        else if (key == "fileBasename")
            replacement = QFileInfo(context.file).fileName();
        else if (key == "fileBasenameNoExtension")
            replacement = QFileInfo(context.file).completeBaseName();
        else if (key == "fileDirname")
            replacement = QFileInfo(context.file).absolutePath();
        else if (key == "fileExtname") {
            const auto suffix = QFileInfo(context.file).suffix();
            replacement = suffix.isEmpty() ? QString() : "." + suffix;
        } else if (key == "relativeFile")
            replacement = QDir(context.workspaceFolder).relativeFilePath(context.file);
        else if (key == "selectedText")
            replacement = context.selectedText;
        else if (key == "lineNumber")
            replacement = QString::number(context.lineNumber);
        else if (key == "pathSeparator" || key == "/")
            replacement = QString(QDir::separator());
        else if (key.startsWith("env:") && context.environment.contains(key.mid(4)))
            replacement = context.environment.value(key.mid(4));
        else {
            error = "Unresolved variable: ${" + key + "}";
            return {};
        }
        if (key.startsWith("workspaceFolder") && context.workspaceFolder.isEmpty()) {
            error = "This configuration needs an open workspace.";
            return {};
        }
        if ((key.startsWith("file") || key == "relativeFile") && context.file.isEmpty()) {
            error = "This configuration needs an active file.";
            return {};
        }
        if (replacement.contains(QChar('\0'))) {
            error = "Substitution contains a null character.";
            return {};
        }
        output += replacement;
        offset = match.capturedEnd();
    }
    const auto suffix = text.mid(offset);
    if (suffix.contains("${")) {
        error = "Malformed substitution variable.";
        return {};
    }
    output += suffix;
    if (output.contains(QChar('\0'))) {
        error = "Configuration string contains a null character.";
        return {};
    }
    return output;
}
QJsonValue DebugConfigurations::resolve(const QJsonValue& value, const VariableContext& context,
                                        QString* error) {
    QString problem;
    const auto result = resolveValue(value, context, problem, 0);
    if (error)
        *error = problem;
    return result;
}
} // namespace litecode::debug
