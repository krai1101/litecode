#include "extensions/MarkdownWire.h"
#include <QJsonArray>

namespace litecode::extensions::wire {
QJsonObject encodeMarkdownString(const MarkdownString& markdown) {
    QJsonObject result{{"value", markdown.value}};
    if (markdown.supportHtml) result.insert("supportHtml", *markdown.supportHtml);
    if (markdown.supportThemeIcons) result.insert("supportThemeIcons", *markdown.supportThemeIcons);
    if (const auto flag = std::get_if<bool>(&markdown.isTrusted)) result.insert("isTrusted", *flag);
    else if (const auto options = std::get_if<MarkdownStringTrustedOptions>(&markdown.isTrusted))
        result.insert("isTrusted", QJsonObject{{"enabledCommands", QJsonArray::fromStringList(options->enabledCommands)}});
    if (markdown.baseUri) result.insert("baseUri", markdown.baseUri->toString(QUrl::FullyEncoded));
    return result;
}
std::optional<MarkdownString> decodeMarkdownString(const QJsonObject& value) {
    if (!value.value("value").isString()) return {};
    MarkdownString result(value.value("value").toString());
    const auto boolean = [&](const char* name, std::optional<bool>& field) {
        if (!value.contains(name)) { field.reset(); return true; }
        const auto flag = value.value(name);
        if (!flag.isBool()) return false;
        field = flag.toBool();
        return true;
    };
    if (!boolean("supportHtml", result.supportHtml) || !boolean("supportThemeIcons", result.supportThemeIcons)) return {};
    if (value.contains("baseUri")) {
        if (!value.value("baseUri").isString()) return {};
        const QUrl uri(value.value("baseUri").toString());
        if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty()) return {};
        result.baseUri = uri;
    }
    if (value.contains("isTrusted")) {
        const auto trust = value.value("isTrusted");
        if (trust.isBool()) result.isTrusted = trust.toBool();
        else {
            if (!trust.isObject() || !trust.toObject().value("enabledCommands").isArray()) return {};
            MarkdownStringTrustedOptions options;
            for (const auto& id : trust.toObject().value("enabledCommands").toArray()) {
                if (!id.isString()) return {};
                options.enabledCommands.append(id.toString());
            }
            result.isTrusted = std::move(options);
        }
    }
    return result;
}
} // namespace litecode::extensions::wire
