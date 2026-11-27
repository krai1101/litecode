#include "ToolCallDisplay.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace litecode::codex {
namespace {
QString literal(const QString& token) {
    const auto array = QJsonDocument::fromJson(('[' + token + ']').toUtf8()).array();
    return array.size() == 1 && array.first().isString() ? array.first().toString() : QString{};
}
QJsonObject invocation(const QString& name, const QJsonObject& args) {
    if (name == "exec_command") {
        const auto command = args.value("cmd").toString(args.value("command").toString());
        if (!command.isEmpty())
            return {{"type", "commandExecution"}, {"command", command}};
    } else if (name == "view_image" && args.value("path").isString())
        return {{"type", "imageView"}, {"path", args.value("path")}};
    return {};
}
QJsonObject orchestration(const QString& input) {
    // Quoted strings and comments are single tokens, so commands containing
    // code examples cannot be mistaken for nested tool calls. Dynamic values
    // remain unknown; never evaluate code or guess their contents.
    static const QRegularExpression tokenPattern(
        R"rx("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`|//[^\n]*|/\*[\s\S]*?\*/|[A-Za-z_$][A-Za-z0-9_$]*|[^\s])rx");
    QStringList tokens;
    auto matches = tokenPattern.globalMatch(input);
    while (matches.hasNext()) {
        const auto token = matches.next().captured();
        if (!token.startsWith("//") && !token.startsWith("/*"))
            tokens.append(token);
    }
    for (int i = 0; i + 4 < tokens.size(); ++i) {
        if (tokens[i] != "tools" || tokens[i + 1] != "." || tokens[i + 3] != "(" ||
            tokens[i + 4] != "{")
            continue;
        const auto name = tokens[i + 2];
        if (name != "exec_command" && name != "view_image")
            continue;
        QJsonObject args;
        int depth = 1;
        for (int j = i + 5; j < tokens.size() && depth; ++j) {
            const auto& token = tokens[j];
            if (token == "{" || token == "[" || token == "(")
                ++depth;
            else if (token == "}" || token == "]" || token == ")")
                --depth;
            else if (depth == 1 && j + 3 < tokens.size() && tokens[j + 1] == ":" &&
                     tokens[j + 2].startsWith('"') &&
                     (tokens[j + 3] == "," || tokens[j + 3] == "}")) {
                const auto key = token.startsWith('"') ? literal(token) : token;
                args.insert(key, literal(tokens[j + 2]));
                j += 2;
            }
        }
        if (const auto result = invocation(name, args); !result.isEmpty())
            return result;
    }
    return {};
}
} // namespace
QJsonObject toolCallDisplay(const QJsonObject& payload) {
    const auto name = payload.value("name").toString();
    const auto type = payload.value("type").toString();
    if (type == "function_call") {
        const auto args = QJsonDocument::fromJson(payload.value("arguments").toString().toUtf8())
                              .object();
        if (const auto result = invocation(name.section('.', -1), args); !result.isEmpty())
            return result;
    } else if (type == "custom_tool_call" && name.section('.', -1) == "exec") {
        if (const auto result = orchestration(payload.value("input").toString()); !result.isEmpty())
            return result;
    }
    return {{"type", "dynamicToolCall"}, {"tool", name},
            {"arguments", type == "custom_tool_call" ? payload.value("input")
                                                    : payload.value("arguments")}};
}
} // namespace litecode::codex
