#include "CodexViewControls.h"
#include <QDir>
namespace litecode::codex {
using namespace extensions;
using namespace view;
namespace {
QString displayPath(const QString& path, const QString& workspace) {
    const auto normalized = QString(path).replace('\\', '/');
    if (workspace.isEmpty() || QDir::isRelativePath(normalized))
        return normalized;
    const auto relative = QDir(workspace).relativeFilePath(normalized);
    return relative == ".." || relative.startsWith("../") ? normalized : relative;
}
NativeControl stats(const QString& id, int plus, int minus) {
    auto row = control(id, NativeControlKind::Row);
    row.spacing = 4;
    auto added = control(id + ".added", NativeControlKind::Label, "+" + QString::number(plus));
    added.appearance = NativeControlAppearance::Added;
    auto removed = control(id + ".removed", NativeControlKind::Label, "-" + QString::number(minus));
    removed.appearance = NativeControlAppearance::Removed;
    row.children = {added, removed};
    return row;
}
} // namespace
NativeControl codexChangesLayout(const CodexTurnView& turn, const CodexViewState& state,
                                 bool latest) {
    const auto id = "codex.turn." + turn.key;
    auto card = control(id + ".changes", NativeControlKind::Column);
    card.appearance = NativeControlAppearance::Surface;
    card.margin = 0;
    card.spacing = 0;
    card.visible = turn.status != "inProgress" && turn.status != "in_progress" &&
                   (!turn.files.isEmpty() || !turn.diff.isEmpty());
    int additions = 0, deletions = 0;
    for (const auto& file : turn.files) {
        additions += file.additions;
        deletions += file.deletions;
    }
    auto title = control(id + ".changedLabel", NativeControlKind::Label,
                         turn.files.isEmpty() ? "File changes"
                                              : QString("Edited %1 file%2")
                                                    .arg(turn.files.size())
                                                    .arg(turn.files.size() == 1 ? "" : "s"));
    title.appearance = NativeControlAppearance::Heading;
    auto summary = control(id + ".changesSummary", NativeControlKind::Column);
    summary.stretch = 1;
    summary.spacing = 2;
    auto totals = stats(id + ".changeTotals", additions, deletions);
    totals.children.append(spacer(id + ".totalsSpacer"));
    summary.children = {title, totals};
    auto icon = action(id + ".changesIcon", "$(file-add)", "File changes", false);
    icon.value = 20;
    icon.appearance = NativeControlAppearance::Muted;
    auto iconTile = control(id + ".changesIconTile", NativeControlKind::Column);
    iconTile.appearance = NativeControlAppearance::Card;
    iconTile.margin = 10;
    iconTile.children = {icon};
    auto review = action(id + ".review", "View changes", "Review this turn's changes");
    review.buttonStyle = NativeButtonStyle::Secondary;
    auto heading = control(id + ".changesHeader", NativeControlKind::Row);
    heading.margin = 12;
    heading.spacing = 8;
    heading.children = {iconTile, summary,
                        action("codex.undo." + turn.key, "Undo $(discard)",
                               "Undo this turn's file changes",
                               !state.busy && state.writable && latest && turn.undoAvailable),
                        review};
    card.children.append(heading);
    card.children.append(control(id + ".changesDivider", NativeControlKind::Separator));
    const int visible = state.expandedChanges.contains(turn.key) ? turn.files.size()
                                                                 : qMin(3, int(turn.files.size()));
    for (int i = 0; i < visible; ++i) {
        const auto& file = turn.files[i];
        auto row = control(id + ".file." + QString::number(i), NativeControlKind::Row);
        row.appearance = NativeControlAppearance::HoverGroup;
        row.margin = 6;
        auto path = action(id + ".reviewFile." + QString::number(i),
                           displayPath(file.path, state.workspace), "Review " + file.path);
        path.stretch = 1;
        row.children = {
            path, stats(id + ".fileStats." + QString::number(i), file.additions, file.deletions)};
        card.children.append(row);
    }
    if (turn.files.size() > 3) {
        card.children.append(control(id + ".moreDivider", NativeControlKind::Separator));
        auto footer = control(id + ".changesFooter", NativeControlKind::Row);
        footer.margin = 10;
        footer.children = {action(id + ".moreFiles",
                                  state.expandedChanges.contains(turn.key)
                                      ? "Show less $(chevron-up)"
                                      : QString("Show %1 more file%2 $(chevron-down)")
                                            .arg(turn.files.size() - 3)
                                            .arg(turn.files.size() - 3 == 1 ? "" : "s"),
                                  "Expand or collapse changed files"),
                           spacer(id + ".footerSpacer")};
        card.children.append(footer);
    }
    return card;
}
} // namespace litecode::codex
