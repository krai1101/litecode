#pragma once

#include <QString>

namespace litecode::ui::components {

struct ComponentTokens;

[[nodiscard]] QString componentStyleSheet(const ComponentTokens& tokens);

} // namespace litecode::ui::components
