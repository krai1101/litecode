#pragma once
class ScintillaEditBase;

namespace litecode::editor::detail {
// Private integration with the pinned, statically linked Scintilla engine.
// The standard BeginUndoAction message cannot request prior coalescing.
// Never reopen a prior action that has already been explicitly closed.
bool beginCoalescingUndoAction(ScintillaEditBase& control, bool allowPrior);
void endCoalescingUndoAction(ScintillaEditBase& control);
} // namespace litecode::editor::detail
