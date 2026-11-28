#include "CodexViewControls.h"
namespace litecode::codex {
using namespace extensions;
using namespace view;
NativeControl codexApprovalLayout(const CodexViewState& state) {
    const auto id = state.approvalKey;
    auto card = control("codex.approvalCard", NativeControlKind::Column);
    card.visible = state.approval.has_value();
    card.appearance = NativeControlAppearance::Composer;
    card.margin = 16;
    card.spacing = 12;
    if (!state.approval) return card;
    const auto& prompt = *state.approval;
    auto header = action(id + ".type", "$(terminal) " + prompt.category, prompt.category, false);
    header.appearance = NativeControlAppearance::Muted;
    card.children.append(header);
    card.children.append(control(id + ".title", NativeControlKind::Label, prompt.title));
    if (state.approvalExpanded) {
        auto details = control(id + ".details", NativeControlKind::TextEdit, prompt.detail);
        details.readOnly = true;
        card.children.append(details);
    } else {
        auto preview = control(id + ".preview", NativeControlKind::Label,
                               prompt.preview.left(220) + (prompt.preview.size() > 220 ? "\u2026" : ""));
        card.children.append(preview);
    }
    auto expand = action(id + ".expand", state.approvalExpanded ? "Collapse" : "Expand",
                         state.approvalExpanded ? "Collapse approval details" : "Expand approval details");
    auto detailActions = control(id + ".detailActions", NativeControlKind::Row);
    detailActions.children = {spacer(id + ".detailSpacer"), expand};
    card.children.append(detailActions);
    auto actions = control(id + ".actions", NativeControlKind::Row);
    actions.children.append(spacer(id + ".spacer"));
    auto more = action(id + ".more", "More options", "More approval options");
    NativeControl allow;
    bool hasAllow = false;
    for (int i = 0; i < prompt.choices.size(); ++i) {
        const auto& choice = prompt.choices[i];
        auto button = action(id + ".choice." + QString::number(i), choice.title, choice.title);
        button.buttonStyle = NativeButtonStyle::Secondary;
        if (choice.isCloseAffordance.value_or(false)) actions.children.append(button);
        else if (!hasAllow) {
            allow = button;
            allow.buttonStyle = NativeButtonStyle::Primary;
            hasAllow = true;
        } else more.children.append(button);
    }
    if (!more.children.isEmpty()) actions.children.append(more);
    if (hasAllow) actions.children.append(allow);
    card.children.append(actions);
    return card;
}
} // namespace litecode::codex
