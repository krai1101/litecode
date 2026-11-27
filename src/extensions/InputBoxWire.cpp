#include "extensions/InputBoxWire.h"
#include <QJsonArray>
#include <limits>
namespace litecode::extensions::wire {
QJsonObject encodeInputBoxOptions(const InputBoxOptions& options) {
    QJsonObject result;
    for (const auto& [name, value] : {std::pair{"title", options.title}, {"value", options.value},
        {"prompt", options.prompt}, {"placeHolder", options.placeHolder}}) if (value) result.insert(name, *value);
    if (options.password) result.insert("password", *options.password);
    if (options.ignoreFocusOut) result.insert("ignoreFocusOut", *options.ignoreFocusOut);
    if (options.valueSelection) result.insert("valueSelection", QJsonArray{options.valueSelection->first, options.valueSelection->second});
    return result;
}
std::optional<InputBoxOptions> decodeInputBoxOptions(const QJsonObject& object) {
    InputBoxOptions result;
    for (const auto& [name, target] : {std::pair{"title", &result.title}, {"value", &result.value},
        {"prompt", &result.prompt}, {"placeHolder", &result.placeHolder}}) {
        if (!object.contains(name)) continue;
        if (!object.value(name).isString()) return {};
        *target = object.value(name).toString();
    }
    for (const auto& [name, target] : {std::pair{"password", &result.password}, {"ignoreFocusOut", &result.ignoreFocusOut}}) {
        if (!object.contains(name)) continue;
        if (!object.value(name).isBool()) return {};
        *target = object.value(name).toBool();
    }
    if (object.contains("valueSelection")) {
        const auto value = object.value("valueSelection");
        if (!value.isArray() || value.toArray().size() != 2) return {};
        const auto array = value.toArray();
        for (const auto& index : array)
            if (!index.isDouble() || index.toDouble() < std::numeric_limits<int>::min() ||
                index.toDouble() > std::numeric_limits<int>::max() || index.toDouble() != index.toInt()) return {};
        result.valueSelection = QPair{array[0].toInt(), array[1].toInt()};
    }
    return result;
}
QJsonValue encodeInputBoxValidation(const InputBoxValidationResult& result) {
    if (const auto* text = std::get_if<QString>(&result)) return *text;
    if (const auto* message = std::get_if<InputBoxValidationMessage>(&result))
        return QJsonObject{{"message", message->message}, {"severity", static_cast<int>(message->severity)}};
    return QJsonValue(QJsonValue::Null);
}
std::optional<InputBoxValidationResult> decodeInputBoxValidation(const QJsonValue& result) {
    if (result.isNull()) return InputBoxValidationResult(std::monostate{});
    if (result.isString()) return InputBoxValidationResult(result.toString());
    if (!result.isObject()) return {};
    const auto object = result.toObject();
    const auto severity = object.value("severity");
    if (!object.value("message").isString() || !severity.isDouble() || severity.toDouble() != severity.toInt() ||
        severity.toInt() < 1 || severity.toInt() > 3) return {};
    return InputBoxValidationResult(InputBoxValidationMessage{object.value("message").toString(),
        static_cast<InputBoxValidationSeverity>(severity.toInt())});
}
} // namespace litecode::extensions::wire
