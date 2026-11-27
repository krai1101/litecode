#include "CodexViewLayout.h"
#include <QJsonObject>
namespace litecode::codex {
QJsonObject codexFastTier(const CodexViewState& state) {
    for (const auto& entry : state.models) {
        const auto model = entry.toObject();
        if (model.value("model").toString(model.value("id").toString()) != state.model)
            continue;
        for (const auto& value : model.value("serviceTiers").toArray()) {
            const auto tier = value.toObject();
            if (tier.value("id") == "priority")
                return tier;
        }
    }
    return {};
}
} // namespace litecode::codex
