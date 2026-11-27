#include "editor/DiffModel.h"
#include <algorithm>
namespace litecode::editor {
InlineDiff makeInlineDiff(const QByteArray& original, const QByteArray& modified,
                          const QVector<DiffHunk>& hunks, int contextLines) {
    auto lines = [](const QByteArray& text) {
        auto result = text.split('\n');
        if (text.isEmpty() || text.endsWith('\n'))
            result.removeLast();
        return result;
    };
    const auto a = lines(original), b = lines(modified);
    InlineDiff result;
    auto append = [&](const QByteArray& text, int oldLine, int newLine, char kind,
                      qint64 changedStart = 0, qint64 changedLength = 0) {
        const int row = result.labels.size();
        result.labels.append(QString("%1 %2 %3")
                                 .arg(oldLine ? QString::number(oldLine) : QString(), 4)
                                 .arg(newLine ? QString::number(newLine) : QString(), 4)
                                 .arg(QChar(kind)));
        if (kind == '-')
            result.removed.append(row);
        if (kind == '+')
            result.added.append(row);
        if (changedLength > 0) {
            auto& spans = kind == '-' ? result.removedSpans : result.addedSpans;
            spans.append({result.text.size() + changedStart, changedLength});
        }
        result.text += text + '\n';
    };
    int oldLine = 0, newLine = 0;
    for (const auto& hunk : hunks) {
        while (oldLine < hunk.originalStart && newLine < hunk.modifiedStart) {
            append(b[newLine], oldLine + 1, newLine + 1, ' ');
            ++oldLine;
            ++newLine;
        }
        result.changeRows.append(result.labels.size());
        QVector<QPair<int, int>> oldSpans, newSpans;
        for (int i = 0; i < qMax(hunk.originalCount, hunk.modifiedCount); ++i) {
            const auto oldText = i < hunk.originalCount ? a.value(oldLine + i) : QByteArray{};
            const auto newText = i < hunk.modifiedCount ? b.value(newLine + i) : QByteArray{};
            int prefix = 0, suffix = 0;
            while (prefix < oldText.size() && prefix < newText.size() &&
                   oldText[prefix] == newText[prefix])
                ++prefix;
            while (suffix < oldText.size() - prefix && suffix < newText.size() - prefix &&
                   oldText[oldText.size() - suffix - 1] == newText[newText.size() - suffix - 1])
                ++suffix;
            const bool paired = i < hunk.originalCount && i < hunk.modifiedCount;
            oldSpans.append(
                {prefix, paired ? static_cast<int>(oldText.size()) - prefix - suffix : 0});
            newSpans.append(
                {prefix, paired ? static_cast<int>(newText.size()) - prefix - suffix : 0});
        }
        for (int i = 0; i < hunk.originalCount; ++i)
            append(a.value(oldLine + i), oldLine + i + 1, 0, '-', oldSpans[i].first,
                   oldSpans[i].second);
        for (int i = 0; i < hunk.modifiedCount; ++i)
            append(b.value(newLine + i), 0, newLine + i + 1, '+', newSpans[i].first,
                   newSpans[i].second);
        oldLine += hunk.originalCount;
        newLine += hunk.modifiedCount;
    }
    while (newLine < b.size()) {
        append(b[newLine], oldLine + 1, newLine + 1, ' ');
        ++oldLine;
        ++newLine;
    }
    if (contextLines < 0)
        return result;
    const int count = result.labels.size();
    QVector<bool> visible(count, false);
    auto include = [&](int row) {
        for (int index = qMax(0, row - contextLines); index <= qMin(count - 1, row + contextLines);
             ++index)
            visible[index] = true;
    };
    for (int row : result.removed)
        include(row);
    for (int row : result.added)
        include(row);
    InlineDiff compact;
    const auto displayLines = result.text.split('\n');
    QVector<int> mapped(count, -1);
    QVector<qint64> oldOffsets, newOffsets;
    qint64 offset = 0;
    int omitted = 0;
    auto separator = [&] {
        if (!omitted)
            return;
        compact.labels.append("...");
        compact.text += "@@ " + QByteArray::number(omitted) + " unchanged lines @@\n";
        omitted = 0;
    };
    for (int row = 0; row < count; ++row) {
        oldOffsets.append(offset);
        if (visible[row]) {
            separator();
            mapped[row] = compact.labels.size();
            newOffsets.append(compact.text.size());
            compact.labels.append(result.labels[row]);
            compact.text += displayLines[row] + '\n';
        } else {
            ++omitted;
            newOffsets.append(-1);
        }
        offset += displayLines[row].size() + 1;
    }
    if (!result.changeRows.isEmpty())
        separator();
    for (int row : result.removed)
        compact.removed.append(mapped[row]);
    for (int row : result.added)
        compact.added.append(mapped[row]);
    for (int row : result.changeRows)
        compact.changeRows.append(mapped[row]);
    auto remapSpans = [&](const auto& spans, auto& output) {
        for (const auto& span : spans) {
            const auto it = std::upper_bound(oldOffsets.cbegin(), oldOffsets.cend(), span.first);
            const int row = static_cast<int>(it - oldOffsets.cbegin()) - 1;
            if (row >= 0 && newOffsets[row] >= 0)
                output.append({newOffsets[row] + span.first - oldOffsets[row], span.second});
        }
    };
    remapSpans(result.removedSpans, compact.removedSpans);
    remapSpans(result.addedSpans, compact.addedSpans);
    return compact;
}
QVector<DiffHunk> compareLines(const QByteArray& original, const QByteArray& modified) {
    if (original == modified)
        return {};
    auto splitLines = [](const QByteArray& bytes) {
        QList<QByteArray> lines;
        qsizetype start = 0;
        for (qsizetype end = bytes.indexOf('\n'); end >= 0; end = bytes.indexOf('\n', start)) {
            lines.append(bytes.mid(start, end - start + 1));
            start = end + 1;
        }
        if (start < bytes.size())
            lines.append(bytes.mid(start));
        return lines;
    };
    const auto a = splitLines(original), b = splitLines(modified);
    int prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix])
        ++prefix;
    int endA = static_cast<int>(a.size()), endB = static_cast<int>(b.size());
    while (endA > prefix && endB > prefix && a[endA - 1] == b[endB - 1]) {
        --endA;
        --endB;
    }
    const int n = endA - prefix, m = endB - prefix;
    const int limit = std::min(n + m, 1024), offset = limit + 1;
    QVector<int> frontier(2 * limit + 3, -1);
    frontier[offset + 1] = 0;
    QVector<QVector<int>> trace;
    int distance = -1;
    for (int d = 0; d <= limit && distance < 0; ++d) {
        trace.push_back(frontier);
        for (int k = -d; k <= d; k += 2) {
            int x = k == -d || (k != d && frontier[offset + k - 1] < frontier[offset + k + 1])
                        ? frontier[offset + k + 1]
                        : frontier[offset + k - 1] + 1;
            int y = x - k;
            while (x < n && y < m && a[prefix + x] == b[prefix + y]) {
                ++x;
                ++y;
            }
            frontier[offset + k] = x;
            if (x >= n && y >= m) {
                distance = d;
                break;
            }
        }
    }
    if (distance < 0)
        return {{prefix, n, prefix, m}};
    QVector<char> edits;
    int x = n, y = m;
    for (int d = distance; d >= 0; --d) {
        const auto& v = trace[d];
        const int k = x - y;
        const int previousK =
            k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1]) ? k + 1 : k - 1;
        const int previousX = d == 0 ? 0 : v[offset + previousK];
        const int previousY = d == 0 ? 0 : previousX - previousK;
        while (x > previousX && y > previousY) {
            edits.push_back('=');
            --x;
            --y;
        }
        if (d > 0) {
            if (x == previousX) {
                edits.push_back('+');
                --y;
            } else {
                edits.push_back('-');
                --x;
            }
        }
    }
    std::reverse(edits.begin(), edits.end());
    QVector<DiffHunk> hunks;
    x = y = prefix;
    bool active = false;
    for (char edit : edits) {
        if (edit == '=') {
            ++x;
            ++y;
            active = false;
            continue;
        }
        if (!active) {
            hunks.push_back({x, 0, y, 0});
            active = true;
        }
        if (edit == '-') {
            ++hunks.last().originalCount;
            ++x;
        } else {
            ++hunks.last().modifiedCount;
            ++y;
        }
    }
    return hunks;
}
} // namespace litecode::editor
