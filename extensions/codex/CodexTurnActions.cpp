#include "CodexTurnActions.h"
#include "CodexTurnPresentation.h"
#include "codex/TurnUndo.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
namespace litecode::codex {
using namespace extensions;
CodexTurnActions::CodexTurnActions(ExtensionContext& context, ChatSession& session,
                                   std::function<void(QString)> notice, QObject* parent)
    : QObject(parent), context_(context), session_(session), notice_(std::move(notice)) {
    connect(
        &session_, &ChatSession::fileChangesProposed, this,
        [this](const QString& thread, const QString& turn, const QJsonArray& changes) {
            if (!TurnUndo::captureBefore(recordPath(thread, turn), session_.workspace(), changes))
                notice_("Could not preserve the pre-edit snapshot. File undo may be unavailable.");
        });
}
QString CodexTurnActions::recordPath(const QString& thread, const QString& turn) const {
    const auto base = context_.storageUri().value_or(context_.globalStorageUri()).toLocalFile();
    return QDir(base).filePath(
        "turn-undo/" +
        QString::fromLatin1(
            QCryptographicHash::hash((thread + '/' + turn).toUtf8(), QCryptographicHash::Sha256)
                .toHex()) +
        ".json");
}
QVector<TurnUndo::Snapshot> CodexTurnActions::reviewSnapshots(const QString& turn,
                                                              const QString& diff,
                                                              const QStringList& paths) const {
    const auto workspace = session_.threadWorkspace();
    TurnUndo recorded(diff, workspace, recordPath(session_.threadId(), turn));
    auto snapshots = recorded.snapshots();
    if (!snapshots.isEmpty())
        return snapshots;
    // One unavailable file must not block reviewing a different file in the
    // same turn. Reconstruct complete snapshots for each requested file using
    // the original patch; never put patch headers into document contents.
    for (const auto& path : paths) {
        const auto patch = codexFileDiff(diff, path);
        if (patch.isEmpty())
            continue;
        // Reviewing an explicitly selected absolute file is read-only and can
        // cross workspace boundaries. Undo keeps its original workspace scope.
        // This temporary reconstruction has no record path and is never applied.
        const auto reviewRoot = QDir::isAbsolutePath(path) ? QFileInfo(path).absolutePath()
                                                          : workspace;
        TurnUndo file(patch, reviewRoot);
        snapshots += file.snapshots();
    }
    return snapshots;
}
QString CodexTurnActions::reviewLast() const {
    const auto& turns = session_.turns();
    for (auto it = turns.crbegin(); it != turns.crend(); ++it) {
        if (!it->diff.isEmpty())
            return it->diff;
        const auto diff =
            TurnUndo::recordedDiff(recordPath(session_.threadId(), it->id), session_.workspace());
        if (!diff.isEmpty())
            return diff;
    }
    return {};
}
void CodexTurnActions::undoLast() {
    if (!session_.canWrite()) {
        notice_("Open this chat for editing before undoing files.");
        return;
    }
    if (session_.busy()) {
        notice_("Stop Codex before undoing files.");
        return;
    }
    const auto& turns = session_.turns();
    for (auto it = turns.crbegin(); it != turns.crend(); ++it) {
        const auto record = recordPath(session_.threadId(), it->id);
        const auto diff =
            it->diff.isEmpty() ? TurnUndo::recordedDiff(record, session_.workspace()) : it->diff;
        if (diff.isEmpty())
            continue;
        auto undo = std::make_shared<TurnUndo>(diff, session_.workspace(), record);
        if (!undo->available()) {
            notice_(undo->reason());
            return;
        }
        MessageOptions options;
        options.modal = true;
        options.detail = undo->paths().join('\n');
        const auto workspace = session_.workspace(), thread = session_.threadId();
        context_.window()
            .showWarningMessage("Undo this turn's file changes?", options,
                                QStringList{"Undo", "Cancel"})
            .then(this, [this, undo, workspace, thread](const std::optional<QString>& choice) {
                if (choice != "Undo")
                    return;
                if (session_.workspace() != workspace || session_.threadId() != thread) {
                    notice_("Undo canceled because the conversation changed.");
                    return;
                }
                for (const auto& document : context_.workspace().textDocuments()) {
                    if (!document->isDirty() || !document->uri().isLocalFile())
                        continue;
                    const auto documentPath = QFileInfo(document->fileName()).absoluteFilePath();
                    for (const auto& path : undo->paths()) {
#ifdef Q_OS_WIN
                        constexpr auto sensitivity = Qt::CaseInsensitive;
#else
                        constexpr auto sensitivity = Qt::CaseSensitive;
#endif
                        if (documentPath.compare(QFileInfo(path).absoluteFilePath(), sensitivity) ==
                            0) {
                            notice_("Save or discard unsaved editor changes before undoing these "
                                    "files.");
                            return;
                        }
                    }
                }
                QString error;
                notice_(undo->apply(&error) ? "File changes undone." : error);
            });
        return;
    }
    notice_("No file changes are available to undo.");
}
void CodexTurnActions::editPrompt(std::function<void(QString, QString, QString)> selected) {
    if (session_.busy()) {
        notice_("Stop Codex before editing an earlier prompt.");
        return;
    }
    struct Prompt {
        QString turn, item, text;
    };
    QVector<Prompt> prompts;
    QVector<QuickPickItem> choices;
    for (const auto& turn : session_.turns())
        for (const auto& item : turn.items) {
            if (item.value("type") != QJsonValue("userMessage"))
                continue;
            QString text;
            for (const auto& part : item.value("content").toArray())
                if (part.toObject().value("type") == QJsonValue("text"))
                    text += part.toObject().value("text").toString();
            if (text.trimmed().isEmpty())
                continue;
            prompts.append({turn.id, item.value("id").toString(), text});
            QuickPickItem choice;
            choice.label = text.left(120).replace('\n', ' ');
            choice.description = QString::number(prompts.size() - 1);
            choices.append(choice);
        }
    if (choices.isEmpty()) {
        notice_("No earlier user prompt is available to edit.");
        return;
    }
    context_.window().showQuickPick(choices).then(
        this, [this, workspace = session_.workspace(), thread = session_.threadId(), prompts,
               selected = std::move(selected)](
                  const std::optional<QuickPickSelection<QuickPickItem>>& choice) {
            if (!choice)
                return;
            if (session_.workspace() != workspace || session_.threadId() != thread) {
                notice_("Prompt selection expired because the conversation changed.");
                return;
            }
            const auto* item = std::get_if<QuickPickItem>(&*choice);
            if (!item || !item->description)
                return;
            bool valid{};
            const auto index = item->description->toInt(&valid);
            if (valid && index >= 0 && index < prompts.size())
                selected(prompts[index].turn, prompts[index].item, prompts[index].text);
        });
}
} // namespace litecode::codex
