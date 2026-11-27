#include "CodexViewProvider.h"
#include <QFileInfo>
namespace litecode::codex {
using namespace extensions;
void CodexViewProvider::openAttachments() {
    OpenDialogOptions options;
    options.canSelectMany = true;
    context_.window().showOpenDialog(options).then(
        this, [this](const std::optional<QVector<QUrl>>& files) {
            if (files)
                for (const auto& uri : *files)
                    if (uri.isLocalFile() && !state_.attachments.contains(uri.toLocalFile()))
                        state_.attachments.append(uri.toLocalFile());
            render();
        });
}
bool CodexViewProvider::handleComposerAction(const NativeViewEvent& event) {
    if (event.controlId == "codex.permissions") {
        if (!session_.busy() && event.index >= 0 && event.index <= 2) {
            state_.approvalMode = static_cast<ApprovalMode>(event.index);
            render();
        }
        return true;
    }
    if (event.controlId == "codex.clearMode") {
        state_.goalMode = state_.planMode = false;
        context_.workspaceState().update("planMode", false);
        render();
        return true;
    }
    if (event.controlId == "codex.fast") {
        if (!session_.busy() && !codexFastTier(state_).isEmpty()) {
            state_.fastMode = !state_.fastMode;
            context_.globalState().update("fastMode", state_.fastMode);
            render();
        }
        return true;
    }
    if (event.controlId == "codex.removeEditorContext") {
        if (!session_.busy()) {
            state_.includeEditor = false;
            render();
        }
        return true;
    }
    if (event.controlId == "codex.attach") {
        if (event.type != "selected" || session_.busy())
            return true;
        if (event.index == 0)
            openAttachments();
        else if (event.index == 3) {
            state_.includeEditor = !state_.includeEditor;
            render();
        } else if ((event.index == 1 || event.index == 2) && !state_.editing) {
            if (event.index == 1) {
                state_.goalMode = !state_.goalMode;
                state_.planMode = false;
            } else {
                state_.planMode = !state_.planMode;
                state_.goalMode = false;
            }
            context_.workspaceState().update("planMode", state_.planMode);
            render();
        }
        return true;
    }
    if (event.controlId == "codex.model") {
        if (session_.busy() || event.index < 0 || event.index >= state_.models.size())
            return true;
        const auto model = state_.models[event.index].toObject();
        state_.model = model.value("model").toString(model.value("id").toString());
        normalizeModel();
        context_.globalState().update("model", state_.model);
        render();
        return true;
    }
    if (event.controlId == "codex.effort") {
        if (session_.busy())
            return true;
        for (const auto& value : state_.models) {
            const auto model = value.toObject();
            if (model.value("model").toString(model.value("id").toString()) != state_.model)
                continue;
            const auto efforts = model.value("supportedReasoningEfforts").toArray();
            if (event.index < 0 || event.index >= efforts.size())
                return true;
            state_.effort = efforts[event.index].toObject().value("reasoningEffort").toString();
            context_.globalState().update("effort", state_.effort);
            break;
        }
        return true;
    }
    return false;
}
} // namespace litecode::codex
