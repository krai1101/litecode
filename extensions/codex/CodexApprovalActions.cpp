#include "CodexViewProvider.h"
namespace litecode::codex {
void CodexViewProvider::showPendingApproval() {
    state_.approval.reset();
    state_.approvalKey.clear();
    state_.approvalExpanded = false;
    if (!approvals_.isEmpty()) {
        state_.approval = approvals_.first().prompt;
        state_.approvalKey = approvals_.first().key;
    }
    render();
}
bool CodexViewProvider::handleApprovalAction(const extensions::NativeViewEvent& event) {
    if (!event.controlId.startsWith("codex.approval.")) return false;
    if (event.type != "clicked" || approvals_.isEmpty()) return true;
    const auto key = approvals_.first().key;
    if (event.controlId == key + ".expand") {
        state_.approvalExpanded = !state_.approvalExpanded;
        render();
    } else if (event.controlId.startsWith(key + ".choice.")) {
        bool valid = false;
        const int index = event.controlId.mid(key.size() + 8).toInt(&valid);
        if (!valid || index < 0 || index >= approvals_.first().prompt.choices.size()) return true;
        const auto approval = approvals_.takeFirst();
        session_.server().respond(approval.id, codexApprovalResponse(
            approval.method, approval.params, approval.prompt.choices[index]));
        showPendingApproval();
    }
    return true;
}
} // namespace litecode::codex
