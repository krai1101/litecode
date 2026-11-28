#pragma once
#include "extensions/PrimitivesApi.h"
#include <QFuture>
#include <QPair>
#include <QString>
#include <functional>
#include <optional>
#include <variant>

namespace litecode::extensions {
enum class InputBoxValidationSeverity { Info = 1, Warning = 2, Error = 3 };
struct InputBoxValidationMessage final {
    QString message;
    InputBoxValidationSeverity severity{InputBoxValidationSeverity::Error};
};
using InputBoxValidationResult = std::variant<std::monostate, QString, InputBoxValidationMessage>;
using InputBoxValidatorResult =
    std::variant<InputBoxValidationResult, QFuture<InputBoxValidationResult>>;
struct InputBoxOptions final {
    std::optional<QString> title, value, prompt, placeHolder;
    std::optional<QPair<int, int>> valueSelection;
    std::optional<bool> password, ignoreFocusOut;
    std::function<InputBoxValidatorResult(const QString&)> validateInput;
};
} // namespace litecode::extensions
