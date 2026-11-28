#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVector>
namespace litecode::editor {
struct DiffHunk {
    int originalStart{}, originalCount{}, modifiedStart{}, modifiedCount{};
};
// Exact line comparison; bounded Myers search falls back to a single replacement
// for very dissimilar inputs instead of allocating unbounded quadratic memory.
QVector<DiffHunk> compareLines(const QByteArray& original, const QByteArray& modified);
struct InlineDiff {
    QByteArray text;
    QStringList labels;
    QVector<int> removed, added, changeRows;
    QVector<QPair<qint64, qint64>> removedSpans, addedSpans;
};
InlineDiff makeInlineDiff(const QByteArray& original, const QByteArray& modified,
                          const QVector<DiffHunk>& hunks, int contextLines = -1);
} // namespace litecode::editor
