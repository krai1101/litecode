#pragma once

class QDialog;

namespace litecode::ui {

// Applies the shared settings-dialog palette without altering a dialog's layout,
// controls, or behavior.
void applySettingsDialogVisuals(QDialog& dialog);

} // namespace litecode::ui
