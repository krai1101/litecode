#include "ui/extensions/EditorSelectionTracking.h"
#include <algorithm>

namespace litecode::ui {
namespace {
enum class MarkerSemantics { Defined, ForceMove, ForceStay };
bool before(qint64 marker, bool stickToPrevious, qint64 boundary, MarkerSemantics semantics) {
    if (marker != boundary) return marker < boundary;
    if (semantics == MarkerSemantics::ForceMove) return false;
    if (semantics == MarkerSemantics::ForceStay) return true;
    return stickToPrevious;
}
qint64 moveMarker(qint64 marker, bool stickToPrevious, const PreparedEditorOperation& operation) {
    const auto start = operation.utf16Offset;
    const auto removed = operation.removedUtf16Length;
    const auto inserted = operation.insertedUtf16Length;
    const auto end = start + removed;
    const auto common = std::min(removed, inserted);
    const auto first = operation.forceMoveMarkers ? MarkerSemantics::ForceMove
        : removed > 0 ? MarkerSemantics::ForceStay : MarkerSemantics::Defined;
    if (before(marker, stickToPrevious, start, first)) return marker;
    if (common > 0 && !operation.forceMoveMarkers) {
        const auto middle = removed > inserted ? MarkerSemantics::ForceStay : MarkerSemantics::Defined;
        if (before(marker, stickToPrevious, start + common, middle)) return marker;
    }
    const auto last = operation.forceMoveMarkers ? MarkerSemantics::ForceMove : MarkerSemantics::Defined;
    if (before(marker, stickToPrevious, end, last)) return start + inserted;
    return std::max(qint64(0), marker + inserted - removed);
}
}
EditorSelectionOffsets trackEditorSelection(EditorSelectionOffsets selection,
    const QVector<PreparedEditorOperation>& operations) {
    const bool empty = selection.anchor == selection.active;
    const bool reversed = selection.anchor > selection.active;
    auto start = std::min(selection.anchor, selection.active);
    auto end = std::max(selection.anchor, selection.active);
    for (const auto& operation : operations) {
        // oneCursor tracks an AlwaysGrowsWhenTypingAtEdges interval: the start
        // sticks to the previous character while the end does not.
        start = moveMarker(start, true, operation);
        end = moveMarker(end, false, operation);
        if (start > end) end = start;
    }
    // Recovering a cursor from an expanded marker interval must not select the
    // inserted text. Do this once after all operations, matching oneCursor.
    if (empty) start = end;
    return reversed ? EditorSelectionOffsets{end, start} : EditorSelectionOffsets{start, end};
}
} // namespace litecode::ui
