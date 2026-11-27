#include "extensions/DebugAdapterContribution.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <stdexcept>
namespace litecode::extensions {
namespace {
QString validateFields(const QJsonObject& value) {
    for (const auto& key : {"runtime", "program"})
        if (value.contains(key) && (!value.value(key).isString() || value.value(key).toString().contains(QChar('\0'))))
            return QString("Expected debugger executable string: ") + key;
    for (const auto& key : {"args", "runtimeArgs"}) {
        if (!value.contains(key)) continue;
        if (!value.value(key).isArray()) return QString("Expected debugger arguments array: ") + key;
        for (const auto& argument : value.value(key).toArray())
            if (!argument.isString() || argument.toString().contains(QChar('\0'))) return QString("Expected debugger string arguments: ") + key;
    }
    return {};
}
QStringList strings(const QJsonValue& value) {
    QStringList result; for (const auto& argument : value.toArray()) result.append(argument.toString()); return result;
}
QString programPath(const QString& value, const QString& root) {
    return value.isEmpty() || QFileInfo(value).isAbsolute() ? value : QDir(root).absoluteFilePath(value);
}
QString runtimePath(const QString& value, const QString& root) {
    // Match the pinned contribution extractor: only './' runtime paths are
    // extension-relative; plain runtime names are resolved through adapter PATH.
    return value.startsWith("./") ? QDir(root).absoluteFilePath(value) : value;
}
}
DebugAdapterPlatform currentDebugAdapterPlatform() {
#ifdef Q_OS_WIN
    return DebugAdapterPlatform::Windows;
#elif defined(Q_OS_MACOS)
    return DebugAdapterPlatform::MacOS;
#else
    return DebugAdapterPlatform::Linux;
#endif
}
QString validateDebugAdapterContribution(const QJsonObject& contribution) {
    if (const auto error = validateFields(contribution); !error.isEmpty()) return error;
    for (const auto& key : {"win", "winx86", "windows", "osx", "linux"}) {
        if (!contribution.contains(key)) continue;
        if (!contribution.value(key).isObject()) return QString("Expected debugger platform object: ") + key;
        if (const auto error = validateFields(contribution.value(key).toObject()); !error.isEmpty()) return error;
    }
    return {};
}
std::optional<DebugAdapterExecutable> debugAdapterExecutableFromContribution(const QJsonObject& contribution,
    const QString& root, DebugAdapterPlatform platform, bool windowsWow64) {
    if (const auto error = validateDebugAdapterContribution(contribution); !error.isEmpty())
        throw std::invalid_argument(error.toStdString());
    QStringList platformKeys;
    switch (platform) {
    case DebugAdapterPlatform::Windows: platformKeys = windowsWow64 ? QStringList{"win", "windows"} : QStringList{"winx86", "win", "windows"}; break;
    case DebugAdapterPlatform::MacOS: platformKeys = {"osx"}; break;
    case DebugAdapterPlatform::Linux: platformKeys = {"linux"}; break;
    }
    QJsonObject selected;
    for (const auto& key : platformKeys)
        if (contribution.value(key).isObject()) { selected = contribution.value(key).toObject(); break; }
    const auto field = [&](const QString& key) {
        const auto value = selected.value(key);
        // In JS arrays, including an empty argument array, are truthy. An empty
        // program/runtime string instead falls back to the base contribution.
        return value.isArray() || (value.isString() && !value.toString().isEmpty()) ? value : contribution.value(key);
    };
    const auto program = programPath(field("program").toString(), root);
    const auto runtime = runtimePath(field("runtime").toString(), root);
    const auto args = strings(field("args"));
    if (!runtime.isEmpty()) {
        auto combined = strings(field("runtimeArgs"));
        if (!program.isEmpty()) combined.append(program);
        combined.append(args);
        return DebugAdapterExecutable{runtime, combined, {}};
    }
    if (!program.isEmpty()) return DebugAdapterExecutable{program, args, {}};
    return {};
}
} // namespace litecode::extensions
