#include "extensions/NativeViewInputLedger.h"
namespace litecode::extensions {
quint64 NativeViewInputLedger::record(const NativeViewEvent& event) {
    const auto sequence = ++issued_;
    if (event.type == "textChanged" || event.type == "selectionChanged" || event.type == "toggled")
        pending_.insert(event.controlId, {sequence, event});
    return sequence;
}
bool NativeViewInputLedger::acknowledge(quint64 sequence) {
    if (sequence < acknowledged_ || sequence > issued_)
        return false;
    acknowledged_ = sequence;
    for (auto it = pending_.begin(); it != pending_.end();)
        if (it->sequence <= sequence)
            it = pending_.erase(it);
        else
            ++it;
    return true;
}
void NativeViewInputLedger::reconcile(NativeControl& root) const {
    const auto edit = pending_.constFind(root.id);
    if (edit != pending_.cend()) {
        const auto& event = edit->event;
        if (event.type == "textChanged" && !root.password &&
            (root.kind == NativeControlKind::Input || root.kind == NativeControlKind::TextEdit))
            root.text = event.text;
        else if (event.type == "selectionChanged")
            root.currentIndex = event.index;
        else if (event.type == "toggled")
            root.checked = event.checked;
    }
    for (auto& child : root.children)
        reconcile(child);
}
} // namespace litecode::extensions
