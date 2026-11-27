#include "CodexViewControls.h"
namespace litecode::codex {
using namespace extensions;
using namespace view;
namespace {
bool groupedActivity(const CodexProgressView& item) {
    return !item.running && (item.icon == "terminal" || item.icon == "edit");
}
QString activitySummary(const CodexTurnView& turn, int start) {
    bool commands = false, edits = false;
    for (int i = start; i < turn.progress.size() && groupedActivity(turn.progress[i]); ++i) {
        commands |= turn.progress[i].icon == "terminal";
        edits |= turn.progress[i].icon == "edit";
    }
    if (edits)
        return "$(edit) Edited files" + QString(commands ? ", ran commands" : "");
    return "$(terminal) Ran commands";
}
} // namespace
bool codexWorkExpanded(const CodexTurnView& turn, const CodexViewState& state) {
    const bool running = turn.status == "inProgress" || turn.status == "in_progress";
    if (running)
        return true;
    if (state.expandedActivity.contains(turn.key))
        return true;
    if (state.expandedActivity.contains(turn.key + "/collapsed"))
        return false;
    const bool finished =
        turn.status == "completed" || turn.status == "failed" || turn.status == "interrupted";
    return finished && turn.response.isEmpty() && !turn.progress.isEmpty();
}
int codexActivityGroupStart(const CodexTurnView& turn, int index) {
    while (index > 0 && groupedActivity(turn.progress[index - 1]))
        --index;
    return index;
}
bool codexProgressVisible(const CodexTurnView& turn, const CodexViewState& state, int index) {
    return codexWorkExpanded(turn, state) &&
           (!groupedActivity(turn.progress[index]) ||
            state.expandedActivity.contains(turn.key + "/activity." +
                                            QString::number(codexActivityGroupStart(turn, index))));
}
QString codexToolLabel(const CodexProgressView& item) {
    if (item.icon == "terminal") {
        const auto command = item.command.isEmpty() ? item.details.section('\n', 0, 0)
                                                    : item.command;
        return QString(item.running ? "$(terminal) Running " : "$(terminal) Ran ") +
               command.simplified();
    }
    return "$(" + item.icon + ") " + item.text;
}
NativeControl codexWorkLayout(const CodexTurnView& turn, const CodexViewState& state) {
    const auto id = "codex.turn." + turn.key;
    const bool running = turn.status == "inProgress" || turn.status == "in_progress";
    auto work = control(id + ".work", NativeControlKind::Column);
    // History may omit timing. Don't leave an empty disclosure for a turn
    // without activity or a recorded duration.
    work.visible = !turn.progress.isEmpty() || turn.hasWorkTiming || turn.status == "inProgress" ||
                   turn.status == "in_progress";
    work.spacing = 4;
    const bool expanded = codexWorkExpanded(turn, state);
    const bool hasDetails = !turn.progress.isEmpty();
    auto toggle = action(
        id + ".tools",
        turn.workLabel + (hasDetails ? (expanded ? " $(chevron-down)" : " $(chevron-right)") : ""),
        expanded ? "Hide work details" : "Show work details");
    if (running || !hasDetails)
        toggle = control(id + ".tools", NativeControlKind::Label, turn.workLabel);
    toggle.appearance = NativeControlAppearance::Plain;
    work.children.append(toggle);
    auto divider = control(id + ".workDivider", NativeControlKind::Separator);
    work.children.append(divider);
    auto details = control(id + ".workDetails", NativeControlKind::Column);
    details.visible = expanded;
    details.spacing = 12;
    if (!expanded) {
        work.children.append(details);
        return work;
    }
    for (int i = 0; i < turn.progress.size(); ++i) {
        const auto& item = turn.progress[i];
        const auto itemId = id + ".progress." + QString::number(i);
        NativeControl* target = &details;
        if (groupedActivity(item)) {
            const int start = codexActivityGroupStart(turn, i);
            if (start == i) {
                const bool groupExpanded = codexProgressVisible(turn, state, i);
                auto group = action(
                    id + ".activityGroup." + QString::number(i),
                    activitySummary(turn, i) + QString(groupExpanded ? " $(chevron-down)" : ""),
                    groupExpanded ? "Hide activity details" : "Show activity details");
                group.appearance = NativeControlAppearance::Muted;
                auto commands =
                    control(id + ".activities." + QString::number(i), NativeControlKind::Column);
                commands.spacing = 4;
                commands.children.append(group);
                details.children.append(commands);
            }
            target = &details.children.last();
            if (!codexProgressVisible(turn, state, i))
                continue;
        }
        if (item.icon.isEmpty())
            target->children.append(control(itemId, NativeControlKind::Markdown, item.text));
        else {
            auto summary = action(id + ".tool." + QString::number(i), codexToolLabel(item),
                                  item.icon == "terminal" ? item.details.section('\n', 0, 0)
                                                          : "Show tool details");
            summary.appearance = NativeControlAppearance::Muted;
            if (item.icon == "terminal")
                summary.toolTip = item.details.section('\n', 0, 0);
            target->children.append(summary);
            if (state.expandedActivity.contains(turn.key + "/" + QString::number(i))) {
                auto output =
                    control(itemId + ".details", NativeControlKind::TextEdit, item.details);
                output.readOnly = true;
                target->children.append(output);
            }
        }
    }
    work.children.append(details);
    auto thinking = control(id + ".thinkingState", NativeControlKind::Column);
    thinking.visible = turn.thinking;
    thinking.contentsMargins = QMargins(0, 12, 0, 0);
    auto label = control(id + ".thinking", NativeControlKind::Label, "Thinking");
    label.appearance = NativeControlAppearance::Shimmer;
    thinking.children = {label};
    work.children.append(thinking);
    return work;
}
} // namespace litecode::codex
