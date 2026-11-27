#include "extensions/DebugDescriptorWire.h"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace litecode::extensions::wire {
namespace {
bool text(const QJsonValue& value, bool nonempty = false) {
    return value.isString() && (!nonempty || !value.toString().trimmed().isEmpty()) && !value.toString().contains(QChar('\0'));
}
}
QJsonObject encodeDebugAdapterDescriptor(const DebugAdapterDescriptor& descriptor) {
    QJsonObject value;
    if (const auto* executable = std::get_if<DebugAdapterExecutable>(&descriptor)) {
        QJsonArray args; for (const auto& argument : executable->args) args.append(argument);
        value = {{"type", "executable"}, {"command", executable->command}, {"args", args}};
        if (executable->options) {
            QJsonObject options;
            if (executable->options->cwd) options.insert("cwd", *executable->options->cwd);
            if (executable->options->env) {
                QJsonObject env;
                for (auto it = executable->options->env->cbegin(); it != executable->options->env->cend(); ++it) env.insert(it.key(), it.value());
                options.insert("env", env);
            }
            value.insert("options", options);
        }
    } else if (const auto* server = std::get_if<DebugAdapterServer>(&descriptor)) {
        value = {{"type", "server"}, {"port", server->port}};
        if (server->host) value.insert("host", *server->host);
    } else if (const auto* pipe = std::get_if<DebugAdapterNamedPipeServer>(&descriptor)) {
        value = {{"type", "pipeServer"}, {"path", pipe->path}};
    } else {
        throw std::runtime_error("An inline debug adapter requires a host-owned message bridge.");
    }
    if (!decodeDebugAdapterDescriptor(value)) throw std::invalid_argument("Invalid debug adapter descriptor.");
    return value;
}
std::optional<DebugAdapterWireDescriptor> decodeDebugAdapterDescriptor(const QJsonObject& value) {
    if (value.value("type") == QJsonValue("executable")) {
        if (!text(value.value("command"), true) || !value.value("args").isArray()) return {};
        DebugAdapterExecutable executable{value.value("command").toString(), {}, {}};
        for (const auto& argument : value.value("args").toArray()) {
            if (!text(argument)) return {};
            executable.args.append(argument.toString());
        }
        if (value.contains("options")) {
            if (!value.value("options").isObject()) return {};
            DebugAdapterExecutableOptions options;
            const auto input = value.value("options").toObject();
            if (input.contains("cwd")) {
                if (!text(input.value("cwd"))) return {};
                options.cwd = input.value("cwd").toString();
            }
            if (input.contains("env")) {
                if (!input.value("env").isObject()) return {};
                options.env.emplace();
                const auto env = input.value("env").toObject();
                for (auto it = env.begin(); it != env.end(); ++it) {
                    if (it.key().isEmpty() || it.key().contains('=') || it.key().contains(QChar('\0')) || !text(it.value())) return {};
                    options.env->insert(it.key(), it.value().toString());
                }
            }
            executable.options = std::move(options);
        }
        return executable;
    }
    if (value.value("type") == QJsonValue("server")) {
        const auto port = value.value("port");
        if (!port.isDouble() || !std::isfinite(port.toDouble()) || port.toDouble() != port.toInt(-1) || port.toInt() < 1 || port.toInt() > 65535
            || (value.contains("host") && !text(value.value("host")))) return {};
        return DebugAdapterServer{port.toInt(), value.contains("host") ? std::optional<QString>(value.value("host").toString()) : std::nullopt};
    }
    if (value.value("type") == QJsonValue("pipeServer") && text(value.value("path"), true))
        return DebugAdapterNamedPipeServer{value.value("path").toString()};
    if (value.value("type") == QJsonValue("implementation") && text(value.value("endpoint"), true))
        return InlineDebugAdapterEndpoint{value.value("endpoint").toString()};
    return {};
}
} // namespace litecode::extensions::wire
