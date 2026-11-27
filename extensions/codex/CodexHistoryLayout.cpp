#include "CodexViewControls.h"
#include <QDateTime>
#include <QJsonObject>

namespace litecode::codex {
using namespace extensions;
using namespace view;

namespace {
QString threadTitle(const QJsonObject& thread) {
    auto title = thread.value("name").toString().trimmed();
    if (title.isEmpty())
        title = thread.value("preview").toString().simplified().left(100);
    return title.isEmpty() ? "Untitled chat" : title;
}
QString threadAge(const QJsonObject& thread) {
    const auto timestamp = thread.value("updatedAt").toInteger();
    if (timestamp <= 0)
        return {};
    const auto seconds = qMax<qint64>(0, QDateTime::currentSecsSinceEpoch() - timestamp);
    if (seconds < 60)
        return "now";
    if (seconds < 3600)
        return QString::number(seconds / 60) + "m";
    if (seconds < 86400)
        return QString::number(seconds / 3600) + "h";
    if (seconds < 604800)
        return QString::number(seconds / 86400) + "d";
    if (seconds < 2592000)
        return QString::number(seconds / 604800) + "w";
    if (seconds < 31536000)
        return QString::number(seconds / 2592000) + "mo";
    return QString::number(seconds / 31536000) + "y";
}
QString threadRow(const QJsonObject& thread) {
    const auto age = threadAge(thread);
    return threadTitle(thread).replace('\t', ' ') + (age.isEmpty() ? QString{} : "\t" + age);
}
} // namespace

NativeControl codexHistoryLayout(const CodexViewState& s) {
    auto body = control("codex.historyPanel", NativeControlKind::Column);
    body.visible = s.history || s.home;
    body.stretch = s.history ? 1 : 0;
    auto search = control("codex.historySearch", NativeControlKind::Input, s.historyFilter);
    search.placeholder = "Search recent chats";
    search.visible = s.history;
    body.children.append(search);
    auto heading = control("codex.historyHeading", NativeControlKind::Label, "All chats");
    heading.visible = s.history;
    heading.appearance = NativeControlAppearance::Muted;
    body.children.append(heading);
    auto history = control("codex.threads", NativeControlKind::List);
    history.appearance = NativeControlAppearance::Plain;
    history.contentsMargins = QMargins(0, 0, 2 * CodexContentInset, 0);
    history.stretch = 1;
    for (const auto& thread : codexFilteredThreads(s))
        history.items.append(threadRow(thread));
    history.visible = s.history && !history.items.isEmpty();
    history.enabled = !s.busy;
    body.children.append(history);
    auto recent = control("codex.recentThreads", NativeControlKind::List);
    recent.appearance = NativeControlAppearance::Plain;
    recent.contentsMargins = QMargins(0, 0, 2 * CodexContentInset, 0);
    for (int i = 0; i < qMin(3, s.threads.size()); ++i)
        recent.items.append(threadRow(s.threads[i].toObject()));
    recent.visible = !s.history && !recent.items.isEmpty();
    recent.enabled = !s.busy;
    body.children.append(recent);
    auto status = control("codex.emptyHistory", NativeControlKind::Label,
                          !s.historyError.isEmpty()   ? "Couldn't load chats: " + s.historyError
                          : !s.historyLoaded          ? "Connect to Codex to load your chats."
                          : s.historyFilter.isEmpty() ? "No saved chats yet."
                                                      : "No chats match your search.");
    status.appearance = NativeControlAppearance::Muted;
    status.visible = !s.historyError.isEmpty() ||
                     (!s.historyLoading && s.historyLoaded &&
                      (s.history ? history.items.isEmpty() : recent.items.isEmpty()));
    body.children.append(status);
    auto loading = action("codex.historySpinner", "$(loading~spin)", "Loading chat history", false);
    loading.visible = s.historyLoading;
    body.children.append(loading);
    auto retry = action("codex.retryHistory", "$(refresh) Retry", "Reload chat history",
                        s.ready && !s.historyLoading);
    retry.visible = !s.historyError.isEmpty();
    body.children.append(retry);
    const auto allText = s.historyCursor.isEmpty() ? QString("View all (%1)").arg(s.threads.size())
                                                   : QString("View all");
    auto all = action("codex.viewAllHistory", allText, "Show all saved chats", !s.busy);
    all.appearance = NativeControlAppearance::Muted;
    all.visible = !s.history && (s.threads.size() > 3 || !s.historyCursor.isEmpty());
    auto allRow = control("codex.viewAllRow", NativeControlKind::Row);
    allRow.children = {all, spacer("codex.viewAllSpacer")};
    allRow.visible = all.visible;
    body.children.append(allRow);
    auto more = action("codex.moreHistory", "Load more chats", "Load the next page of chats",
                       !s.historyLoading && s.ready);
    more.visible = s.history && !s.historyCursor.isEmpty();
    body.children.append(more);
    if (s.history && history.items.isEmpty())
        body.children.append(spacer("codex.historySpacer"));
    return body;
}
} // namespace litecode::codex
