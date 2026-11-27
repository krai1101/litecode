#include "editor/ScintillaUndoBridge.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <forward_list>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Scintilla internal headers expect their prerequisite headers in this order.
// Isolate this dependency here rather than exposing engine types in our API.
// clang-format off
#include <ScintillaTypes.h>
#include <ILoader.h>
#include <ILexer.h>
#include <Debugging.h>
#include <CharacterType.h>
#include <CharacterCategoryMap.h>
#include <Position.h>
#include <SplitVector.h>
#include <Partitioning.h>
#include <RunStyles.h>
#include <CellBuffer.h>
#include <PerLine.h>
#include <CharClassify.h>
#include <Decoration.h>
#include <CaseFolder.h>
#include <Document.h>
#include <UndoHistory.h>
#include <ScintillaMessages.h>
#include <ScintillaEditBase.h>
// clang-format on

namespace litecode::editor::detail {
namespace {
Scintilla::Internal::Document* document(ScintillaEditBase& control) {
    // SCI_GETDOCPOINTER returns IDocumentEditable, not the most-derived
    // Document address. The interface is a secondary base in Scintilla 5.6.4;
    // a reinterpret_cast would skip its required pointer adjustment.
    auto* editable = reinterpret_cast<Scintilla::IDocumentEditable*>(
        control.send(static_cast<unsigned int>(Scintilla::Message::GetDocPointer)));
    return static_cast<Scintilla::Internal::Document*>(editable);
}
} // namespace
bool beginCoalescingUndoAction(ScintillaEditBase& control, bool allowPrior) {
    auto* model = document(control);
    if (!model)
        return false;
    const auto current = model->UndoCurrent();
    const bool priorOpen = current > 0 && (model->UndoActionType(current - 1) &
                                           Scintilla::Internal::coalesceFlag) != 0;
    model->BeginUndoAction(allowPrior && priorOpen);
    return true;
}
void endCoalescingUndoAction(ScintillaEditBase& control) {
    if (auto* model = document(control))
        model->EndUndoAction();
}
} // namespace litecode::editor::detail
