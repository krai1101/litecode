#include "CodexAttachmentStore.h"
#include "CodexViewControls.h"
#include <QFileInfo>
#include <QJsonObject>
namespace litecode::codex {
using namespace extensions;
using namespace view;
bool codexCanSend(const CodexViewState& s) {
    if (!s.ready || s.busy || (!s.home && (!s.writable || s.readOnly || !s.openError.isEmpty())) ||
        (s.draft.trimmed().isEmpty() && s.attachments.isEmpty()))
        return false;
    if (s.goalMode && s.draft.trimmed().isEmpty())
        return false;
    for (const auto& entry : s.models) {
        const auto model = entry.toObject();
        if (model.value("model").toString(model.value("id").toString()) == s.model)
            return true;
    }
    return false;
}
NativeControl codexComposerLayout(const CodexViewState& s) {
    auto box = control("codex.composer", NativeControlKind::Column);
    box.appearance = NativeControlAppearance::Composer;
    box.margin = 12;
    auto edit = control("codex.editNotice", NativeControlKind::Row);
    edit.visible = s.editing;
    auto hint = control("codex.editHint", NativeControlKind::Label, "Editing an earlier message");
    hint.stretch = 1;
    hint.appearance = NativeControlAppearance::Muted;
    edit.children = {hint, action("codex.cancelEdit", "$(close)", "Cancel edit")};
    box.children.append(edit);
    auto attachments = control("codex.attachments", NativeControlKind::Column);
    attachments.visible = !s.attachments.isEmpty();
    auto imageRow = control("codex.attachmentImages", NativeControlKind::Row);
    imageRow.visible = false;
    for (int i = 0; i < qMin(3, int(s.attachments.size())); ++i) {
        const auto id = "codex.attachment." + QString::number(i);
        auto row = control(id, NativeControlKind::Row);
        auto file =
            control(id + ".name", NativeControlKind::Label, QFileInfo(s.attachments[i]).fileName());
        if (codexImageFile(s.attachments[i])) {
            file.kind = NativeControlKind::Image;
            file.text = "Attached image: " + QFileInfo(s.attachments[i]).fileName();
            file.iconPath = s.attachments[i];
            file.value = 120;
            file.stretch = 0;
        }
        if (file.kind == NativeControlKind::Label)
            file.toolTip = s.attachments[i];
        if (file.kind == NativeControlKind::Label)
            file.stretch = 1;
        auto remove = action(id + ".remove", "$(close)", "Remove attachment", !s.busy);
        if (file.kind == NativeControlKind::Image) {
            remove.value = 8;
            file.children = {remove};
            imageRow.visible = true;
            imageRow.children.append(file);
        } else {
            row.children = {file, remove};
            attachments.children.append(row);
        }
    }
    imageRow.children.append(spacer("codex.attachmentImagesSpacer"));
    attachments.children.prepend(imageRow);
    if (s.attachments.size() > 3)
        attachments.children.append(
            action("codex.manageAttachments",
                   "+ " + QString::number(s.attachments.size() - 3) + " more attachments…",
                   "Manage attachments", !s.busy));
    box.children.append(attachments);
    auto prompt = control("codex.prompt", NativeControlKind::TextEdit, s.draft);
    prompt.placeholder =
        (s.home || s.turns.isEmpty()) ? "Do anything" : "Ask for follow-up changes";
    prompt.accessibleName = "Message Codex";
    // Shared chats may reject submission, but local drafts remain editable.
    // codexCanSend and the session enforce conversation ownership.
    auto editorContext = control("codex.editorContext", NativeControlKind::Row);
    editorContext.visible = s.includeEditor;
    auto contextLabel = control("codex.editorContextLabel", NativeControlKind::Label,
                                "Active file / selection included");
    contextLabel.stretch = 1;
    editorContext.children = {contextLabel, action("codex.removeEditorContext", "$(close)",
                                                   "Remove active file context", !s.busy)};
    box.children.append(editorContext);
    auto mode = control("codex.mode", NativeControlKind::Row);
    mode.visible = s.goalMode || s.planMode;
    auto modeLabel =
        control("codex.modeLabel", NativeControlKind::Label, s.goalMode ? "Goal" : "Plan mode");
    modeLabel.stretch = 1;
    mode.children = {modeLabel,
                     action("codex.clearMode", "$(close)", "Return to normal chat", !s.busy)};
    box.children.append(mode);
    box.children.append(prompt);
    auto models = control("codex.model", NativeControlKind::ComboBox);
    models.accessibleName = "Select model";
    models.text = "Select model";
    models.appearance = NativeControlAppearance::Plain;
    models.placeholder = s.ready ? "No models available" : "";
    models.enabled = !s.busy && !s.models.isEmpty();
    for (const auto& value : s.models) {
        const auto model = value.toObject();
        const auto id = model.value("model").toString(model.value("id").toString());
        if (id == s.model)
            models.currentIndex = models.items.size();
        models.items.append(model.value("displayName").toString(id));
    }
    const auto effortLabel = [](QString value) {
        if (!value.isEmpty())
            value[0] = value[0].toUpper();
        return value;
    };
    auto effort = control("codex.effort", NativeControlKind::ChoiceSlider);
    effort.accessibleName = "Reasoning effort";
    effort.enabled = !s.busy && !s.models.isEmpty();
    for (const auto& value : s.models) {
        const auto model = value.toObject();
        if (model.value("model").toString(model.value("id").toString()) == s.model) {
            effort.text = model.value("displayName").toString(s.model);
            const auto defaultEffort = model.value("defaultReasoningEffort").toString();
            for (const auto& option : model.value("supportedReasoningEfforts").toArray()) {
                const auto id = option.toObject().value("reasoningEffort").toString();
                if (id == defaultEffort)
                    effort.value = effort.items.size();
                effort.items.append(effortLabel(id));
            }
        }
    }
    effort.children.append(models);
    effort.currentIndex = effort.items.indexOf(effortLabel(s.effort));
    effort.visible = !effort.items.isEmpty();
    const auto fastTier = codexFastTier(s);
    if (!fastTier.isEmpty()) {
        auto fast = action("codex.fast", s.fastMode ? "$(zap) On" : "$(zap)",
                           (s.fastMode ? "Fast mode on: " : "Enable fast mode: ") +
                               fastTier.value("description").toString(),
                           !s.busy);
        effort.children.append(fast);
    }
    auto controls = control("codex.actions", NativeControlKind::Row);
    auto send = action("codex.send", "$(arrow-up)",
                       s.editing ? "Save and send edit" : "Send message", codexCanSend(s));
    send.buttonStyle = NativeButtonStyle::Primary;
    send.visible = !s.busy;
    auto stop = action("codex.stop", "$(debug-stop)", "Stop generating", s.busy);
    stop.visible = s.busy;
    stop.buttonStyle = NativeButtonStyle::Primary;
    auto attach = action("codex.attach", "$(add)", "Add", !s.busy && !s.editing);
    attach.items = {"$(attach) Files and images", "$(target) Goal\nSet a goal to keep pursuing",
                    "$(lightbulb) Plan mode\n" +
                        QString(s.planMode ? "Turn plan mode off" : "Turn plan mode on"),
                    s.includeEditor ? "Remove active file or selection"
                                    : "Include active file or selection"};
    auto permissions = control("codex.permissions", NativeControlKind::ComboBox);
    permissions.text = "How should Codex actions be approved?";
    permissions.iconSize = 14;
    permissions.items = {
        "Ask for approval\nAlways ask to edit external files and use the internet",
        "Approve for me\nOnly ask for actions detected as potentially unsafe",
        "Full access\nUnrestricted access to the internet and any file on your computer"};
    permissions.itemIconPaths = {"icons/hand.svg", "icons/shield-check.svg",
                                 "icons/shield-alert.svg"};
    permissions.itemColors = {"", "", "#d99500"};
    permissions.helpText = "Learn more";
    permissions.helpUrl = "https://developers.openai.com/codex/security/";
    permissions.currentIndex = static_cast<int>(s.approvalMode);
    permissions.accessibleName =
        "Approval: " + permissions.items[permissions.currentIndex].section('\n', 0, 0);
    permissions.enabled = !s.busy && (s.home || (!s.readOnly && s.writable));
    auto picker = control("codex.modelPicker", NativeControlKind::Row);
    picker.spacing = 0;
    picker.children =
        effort.items.isEmpty() ? QVector<NativeControl>{models} : QVector<NativeControl>{effort};
    if (effort.items.isEmpty())
        for (const auto& child : effort.children)
            if (child.kind == NativeControlKind::Button)
                picker.children.append(child);
    controls.spacing = 4;
    auto usage = control("codex.usage", NativeControlKind::Progress);
    usage.appearance = NativeControlAppearance::Plain;
    usage.accessibleName = "Context window usage";
    usage.visible = !s.home && s.contextLimit > 0;
    const int percent =
        s.contextLimit > 0 ? qBound(0, qRound(100.0 * s.contextUsed / s.contextLimit), 100) : 0;
    const auto tokens = [](qint64 count) {
        return count >= 1000 ? QString::number(count / 1000) + "k" : QString::number(count);
    };
    usage.value = percent;
    usage.text = QString("Context window:\n%1% used (%2% left)\n%3 / %4 tokens used")
                     .arg(percent)
                     .arg(100 - percent)
                     .arg(tokens(s.contextUsed), tokens(s.contextLimit));
    usage.toolTip = usage.text;
    controls.children = {attach, permissions, spacer("codex.composerSpacer"), usage, picker,
                         send,   stop};
    box.children.append(controls);
    return box;
}
} // namespace litecode::codex
