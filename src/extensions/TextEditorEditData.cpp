#include "extensions/TextEditorEditData.h"
#include <algorithm>

namespace litecode::extensions {
bool textEditorEditRangesOverlap(const QVector<TextEditorEditOperation>& edits) {
    // Upstream sorts a separate range list by end, then start. Do not reorder
    // the operations themselves: coincident insertions depend on input order.
    QVector<Range> ranges;
    ranges.reserve(edits.size());
    for (const auto& edit : edits)
        ranges.append(edit.range);
    std::sort(ranges.begin(), ranges.end(), [](const Range& left, const Range& right) {
        const auto endOrder = left.end().compareTo(right.end());
        return endOrder < 0 || (endOrder == 0 && left.start().isBefore(right.start()));
    });
    for (qsizetype index = 1; index < ranges.size(); ++index)
        if (ranges[index].start().isBefore(ranges[index - 1].end()))
            return true;
    return false;
}
} // namespace litecode::extensions
