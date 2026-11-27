#include "ui/extensions/NativeViewScrollState.h"
#include "ui/extensions/NativeTranscriptView.h"
#include <QAbstractButton>
#include <QLabel>
#include <QLayout>
#include <QScrollBar>
#include <QTextBrowser>
#include <algorithm>
namespace litecode::ui {
namespace {
QVector<QWidget*> contentRows(QScrollArea& scroll) {
    QVector<QWidget*> rows;
    if (!scroll.widget() || !scroll.widget()->layout())
        return rows;
    auto* layout = scroll.widget()->layout();
    for (int i = 0; i < layout->count(); ++i) {
        auto* child = layout->itemAt(i)->widget();
        if (!child || child->isHidden())
            continue;
        if (auto* label = qobject_cast<QLabel*>(child); label && label->text().isEmpty())
            continue;
        rows.append(child);
    }
    return rows;
}
} // namespace
NativeViewScrollState NativeViewScrollState::capture(QScrollArea& scroll) {
    NativeViewScrollState state;
    auto* bar = scroll.verticalScrollBar();
    state.position = bar->value();
    state.followEnd = bar->value() >= bar->maximum() - 2;
    if (auto* transcript = qobject_cast<NativeTranscriptView*>(&scroll))
        // Pending text layout can enlarge the range before its follow timer runs.
        // That temporary distance from the bottom is not user navigation.
        state.followEnd = transcript->followingLatest();
    const auto rows = contentRows(scroll);
    if (!rows.isEmpty()) {
        state.first = rows.first()->objectName();
        state.last = rows.last()->objectName();
    }
    // Keep the activated disclosure at the same viewport position, even if
    // expanding content changes the height of its enclosing turn.
    const auto activated = scroll.property("nativeActivatedAnchor").toString();
    scroll.setProperty("nativeActivatedAnchor", {});
    if (auto* anchor = scroll.findChild<QAbstractButton*>(activated);
        !activated.isEmpty() && anchor && anchor->isVisible()) {
        state.followEnd = false;
        state.anchors.append({activated, anchor->mapTo(scroll.viewport(), QPoint{}).y()});
    }
    // Within a turn, work can collapse while its answer is already being read.
    // Anchor visible text before falling back to the enclosing turn's top edge.
    if (qobject_cast<NativeTranscriptView*>(&scroll) && scroll.widget()) {
        QVector<QPair<QString, int>> textAnchors;
        for (auto* text : scroll.widget()->findChildren<QTextBrowser*>()) {
            if (!text->isVisibleTo(scroll.widget()))
                continue;
            const int top = text->mapTo(scroll.viewport(), QPoint{}).y();
            if (top + text->height() > 0 && top < scroll.viewport()->height())
                textAnchors.append({text->objectName(), top});
        }
        std::stable_sort(textAnchors.begin(), textAnchors.end(),
                         [](const auto& a, const auto& b) { return a.second < b.second; });
        state.anchors += textAnchors;
    }
    for (auto* row : rows) {
        const int top = row->y() - state.position;
        if (top + row->height() >= 0 && top <= scroll.viewport()->height())
            state.anchors.append({row->objectName(), top});
    }
    return state;
}
void NativeViewScrollState::restore(QScrollArea& scroll) const {
    const auto rows = contentRows(scroll);
    // Consecutive history windows share a boundary row. Preserve that row's
    // viewport offset rather than jumping to either end of the new window.
    const bool nextWindow =
        !rows.isEmpty() && last != rows.last()->objectName() && last == rows.first()->objectName();
    const bool pinned = followEnd && !nextWindow;
    int target = position;
    QString matchedAnchor;
    int matchedOffset = 0;
    if (!pinned)
        for (const auto& anchor : anchors) {
            auto* found = scroll.widget()->findChild<QWidget*>(anchor.first);
            if (found && found->isVisibleTo(scroll.widget())) {
                target = found->mapTo(scroll.widget(), QPoint{}).y() - anchor.second;
                matchedAnchor = anchor.first;
                matchedOffset = anchor.second;
                break;
            }
        }
    if (auto* transcript = qobject_cast<NativeTranscriptView*>(&scroll)) {
        transcript->restorePosition(target, pinned);
        if (!matchedAnchor.isEmpty())
            transcript->preserveAnchor(matchedAnchor, matchedOffset);
    } else
        scroll.verticalScrollBar()->setValue(pinned ? scroll.verticalScrollBar()->maximum()
                                                    : target);
}
} // namespace litecode::ui
