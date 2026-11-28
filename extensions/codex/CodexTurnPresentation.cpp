#include "CodexTurnPresentation.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>
namespace litecode::codex {
namespace {
QString gitPath(QString path, bool stripPrefix = true) {
    path = path.section('\t', 0, 0);
    if (path.endsWith('\r'))
        path.chop(1);
    if (path.startsWith('"') && path.endsWith('"')) {
        const auto bytes = path.mid(1, path.size() - 2).toUtf8();
        QByteArray decoded;
        for (int i = 0; i < bytes.size(); ++i) {
            if (bytes[i] != '\\' || i + 1 >= bytes.size()) {
                decoded += bytes[i];
                continue;
            }
            const char next = bytes[++i];
            if (next >= '0' && next <= '7') {
                int value = next - '0';
                for (int n = 1;
                     n < 3 && i + 1 < bytes.size() && bytes[i + 1] >= '0' && bytes[i + 1] <= '7';
                     ++n)
                    value = value * 8 + bytes[++i] - '0';
                decoded += char(value);
            } else
                decoded += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
        }
        path = QString::fromUtf8(decoded);
    }
    if (stripPrefix && (path.startsWith("a/") || path.startsWith("b/")))
        path.remove(0, 2);
    return path;
}
QString takeText(const QString& text, int& budget) {
    const int count = qMin(qMax(0, budget), int(text.size()));
    budget -= count;
    return text.left(count);
}
void countChanges(const QString& diff, CodexFileChangeView& file) {
    bool hunk = false;
    for (const auto& line : diff.split('\n')) {
        if (line.startsWith("@@")) {
            hunk = true;
            continue;
        }
        if (line.startsWith("diff --git ")) {
            hunk = false;
            continue;
        }
        if (!hunk)
            continue;
        file.additions += line.startsWith('+');
        file.deletions += line.startsWith('-');
    }
}
QVector<CodexFileChangeView> filesFromDiff(const QString& diff) {
    QVector<CodexFileChangeView> files;
    QString section;
    const auto finish = [&] {
        if (section.isEmpty())
            return;
        CodexFileChangeView file;
        for (const auto& line : section.split('\n')) {
            if (line.startsWith("+++ ") && line.mid(4) != "/dev/null")
                file.path = gitPath(line.mid(4));
            else if (file.path.isEmpty() && line.startsWith("--- "))
                file.path = gitPath(line.mid(4));
            else if (line.startsWith("rename to "))
                file.path = gitPath(line.mid(10), false);
        }
        if (file.path.isEmpty() || file.path == "/dev/null")
            return;
        countChanges(section, file);
        files.append(file);
    };
    for (const auto& line : diff.split('\n')) {
        if (line.startsWith("diff --git ")) {
            finish();
            section.clear();
        }
        section += line + '\n';
    }
    finish();
    return files;
}
} // namespace
QString codexFileDiff(const QString& diff, const QString& path) {
    QString section;
    const auto matches = [&] {
        const auto files = filesFromDiff(section);
        return !files.isEmpty() && files.first().path == path;
    };
    for (const auto& line : diff.split('\n')) {
        if (line.startsWith("diff --git ")) {
            if (matches())
                return section;
            section.clear();
        }
        section += line + '\n';
    }
    return matches() ? section : QString{};
}
QString codexFinalResponse(const TurnState& turn) {
    QString response, fallback;
    bool explicitFinal = false;
    for (const auto& item : turn.items) {
        if (item.value("type") != "agentMessage")
            continue;
        const auto phase = item.value("phase").toString();
        const auto text = item.value("text").toString();
        if (phase == "final_answer") {
            explicitFinal = true;
            if (!text.trimmed().isEmpty())
                response += text + "\n\n";
        } else if (phase != "commentary" && !text.trimmed().isEmpty())
            fallback = text + "\n\n";
    }
    return explicitFinal ? response : fallback;
}
QString codexTurnDiff(const TurnState& turn) {
    if (!turn.diff.isEmpty())
        return turn.diff;
    QString diff;
    for (const auto& item : turn.items)
        if (item.value("type") == "fileChange")
            for (const auto& value : item.value("changes").toArray()) {
                const auto change = value.toObject();
                const auto patch = change.value("diff").toString();
                if (patch.startsWith("diff --git "))
                    diff += patch + '\n';
                else if (patch.contains("@@ ")) {
                    const auto path = change.value("path").toString();
                    const auto type = change.value("kind").toObject().value("type").toString();
                    diff += "diff --git a/" + path + " b/" + path + '\n' + "--- " +
                            (type == "add" ? QString("/dev/null") : "a/" + path) + '\n' + "+++ " +
                            (type == "delete" ? QString("/dev/null") : "b/" + path) + '\n' + patch +
                            '\n';
                }
            }
    return diff;
}
void populateTurnPresentation(const TurnState& turn, CodexTurnView& view, int& budget, bool active,
                              const CodexViewState* presentation) {
    view.status = turn.status;
    // Only visible text consumes the native frame's text budget. The original
    // prompt, patch and tool data remain in the session for copy/edit/review.
    QString prompt;
    for (const auto& item : turn.items)
        if (item.value("type") == "userMessage") {
            view.userItemId = item.value("id").toString();
            for (const auto& part : item.value("content").toArray()) {
                const auto content = part.toObject();
                prompt += content.value("text").toString();
                if (view.images.size() < 16) {
                    const auto type = content.value("type").toString();
                    if (type == "localImage")
                        view.images.append(content.value("path").toString());
                    else if (type == "image")
                        view.images.append(content.value("url").toString());
                }
            }
        }
    if (presentation && !presentation->expandedPrompts.contains(view.key))
        prompt = prompt.left(601); // Preserve the existing Show full message affordance.
    int lastAgent = -1;
    bool phasedFinal = false;
    for (int i = 0; i < turn.items.size(); ++i) {
        const auto& item = turn.items[i];
        if (item.value("type") != "agentMessage")
            continue;
        phasedFinal |= item.value("phase") == "final_answer";
        if (item.value("phase") != "commentary" &&
            !item.value("text").toString().trimmed().isEmpty())
            lastAgent = i;
    }
    // An unphased live message is work until completion establishes it as the answer.
    // Explicit final-answer items can stream in their stable answer slot immediately.
    view.response = takeText(active && !phasedFinal ? QString{} : codexFinalResponse(turn), budget);
    view.responseStreaming = active && !view.response.isEmpty();
    view.thinking = active && !view.responseStreaming && turn.activeTools.isEmpty();
    view.prompt = takeText(prompt, budget);
    const bool terminal =
        turn.status == "completed" || turn.status == "failed" || turn.status == "interrupted";
    const bool showWork = !presentation || active ||
                          (!presentation->expandedActivity.contains(view.key + "/collapsed") &&
                           (presentation->expandedActivity.contains(view.key) ||
                            (terminal && view.response.isEmpty())));
    const auto workText = [&](const QString& text, bool detail = false) {
        const bool visible =
            showWork && (!detail || !presentation ||
                         presentation->expandedActivity.contains(
                             view.key + "/" + QString::number(view.progress.size())));
        return visible ? takeText(text, budget) : text.left(16 * 1024);
    };
    if (turn.startedMs > 0) {
        const auto started = QDateTime::fromMSecsSinceEpoch(turn.startedMs).toLocalTime();
        view.userTime = QLocale().toString(started.time(), QLocale::ShortFormat);
        if (!active && turn.durationMs >= 0)
            view.responseTime =
                QLocale().toString(started.addMSecs(turn.durationMs).time(), QLocale::ShortFormat);
    }
    const auto elapsed = active && turn.startedMs > 0
                             ? QDateTime::currentMSecsSinceEpoch() - turn.startedMs
                             : qMax<qint64>(0, turn.durationMs);
    const auto seconds = qMax<qint64>(0, elapsed / 1000);
    const QString duration = seconds >= 60 ? QString("%1m %2s").arg(seconds / 60).arg(seconds % 60)
                                           : QString("%1s").arg(seconds);
    const bool knownDuration = active ? turn.startedMs > 0 : turn.durationMs >= 0;
    view.hasWorkTiming = knownDuration;
    view.workLabel = knownDuration ? (active ? "Working for " : "Worked for ") + duration
                                   : (active ? "Working" : "Work details");
    for (int i = 0; i < turn.items.size() && budget > 0; ++i) {
        const auto& item = turn.items[i];
        const auto type = item.value("type").toString();
        if (type == "agentMessage") {
            const bool final =
                item.value("phase") == "final_answer" ||
                (!active && !phasedFinal && i == lastAgent && item.value("phase") != "commentary");
            if (!final)
                view.progress.append({workText(item.value("text").toString()), {}, {}});
        } else if (type == "commandExecution") {
            const auto command = item.value("command").toString();
            const auto output = item.value("aggregatedOutput").toString().right(16 * 1024);
            QString details = workText(command + '\n' + output, true);
            const auto group = codexActivityGroupStart(view, view.progress.size());
            if (presentation && showWork &&
                presentation->expandedActivity.contains(view.key + "/activity." +
                                                        QString::number(group)) &&
                !presentation->expandedActivity.contains(view.key + "/" +
                                                         QString::number(view.progress.size())))
                details = workText(command) + '\n' + output;
            const auto status = item.value("status").toString();
            const bool running = active &&
                (turn.activeTools.contains(item.value("id").toString()) ||
                 status == "inProgress" || status == "in_progress");
            // The server's parsed actions omit the shell launcher and quoting.
            // Keep raw command/output for details, but use these for the row.
            QStringList actions;
            for (const auto& value : item.value("commandActions").toArray()) {
                const auto action = value.toObject().value("command").toString();
                if (!action.isEmpty())
                    actions.append(action);
            }
            view.progress.append({running ? "Running" : "Ran commands", details, "terminal",
                                  running, actions.isEmpty() ? command : actions.join("; ")});
            if (running)
                view.thinking = false;
        } else if (type == "fileChange") {
            QStringList paths;
            for (const auto& change : item.value("changes").toArray()) {
                const auto object = change.toObject();
                const auto path = object.value("path").toString();
                paths.append(path);
                CodexFileChangeView file{path};
                countChanges(object.value("diff").toString(), file);
                if (view.files.size() < 200)
                    view.files.append(file);
            }
            view.progress.append({"Edited files", workText(paths.join('\n'), true), "edit"});
        } else if (type == "reasoning") {
            QString summary = item.value("liveSummary").toString();
            if (summary.isEmpty())
                for (const auto& value : item.value("summary").toArray())
                    summary += value.toString() + '\n';
            if (!active && !summary.isEmpty())
                view.progress.append({"Thinking", workText(summary, true), "lightbulb"});
        } else if (type == "contextCompaction") {
            const auto status = item.value("status").toString();
            const bool running = active &&
                (turn.activeTools.contains(item.value("id").toString()) ||
                 status == "inProgress" || status == "in_progress");
            view.progress.append({running ? "Compacting context \u00b7 This can take a few minutes"
                                          : "Context automatically compacted",
                                  {}, "context-compact", running});
            if (running)
                view.thinking = false;
        } else if (type == "webSearch") {
            view.progress.append(
                {"Searched the web", workText(item.value("query").toString(), true), "search"});
        } else if (type == "imageView") {
            const auto status = item.value("status").toString();
            const bool running = active &&
                (turn.activeTools.contains(item.value("id").toString()) ||
                 status == "inProgress" || status == "in_progress");
            view.progress.append({running ? "Viewing an image" : "Viewed an image",
                                  workText(item.value("path").toString(), true), "file-media",
                                  running});
            if (running)
                view.thinking = false;
        } else if (type == "mcpToolCall" || type == "dynamicToolCall" ||
                   type == "collabAgentToolCall") {
            const auto name = item.value("tool").toString(type);
            const auto detail =
                QString::fromUtf8(QJsonDocument(item).toJson(QJsonDocument::Indented))
                    .left(16 * 1024);
            const auto status = item.value("status").toString();
            const bool running = active &&
                (turn.activeTools.contains(item.value("id").toString()) ||
                 status == "inProgress" || status == "in_progress");
            view.progress.append({(running ? "Running " : "Used ") + name,
                                  workText(detail, true), "tools", running});
            if (running)
                view.thinking = false;
        }
    }
    const auto diffFiles = filesFromDiff(codexTurnDiff(turn));
    if (!diffFiles.isEmpty())
        view.files = diffFiles.mid(0, 200);
}
} // namespace litecode::codex
