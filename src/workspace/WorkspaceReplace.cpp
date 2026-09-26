#include "workspace/WorkspaceReplace.h"
#include "core/FileSystemPath.h"
#include "core/TextEncoding.h"
#include "workspace/WorkspaceFileScope.h"
#include "workspace/WorkspaceTextQuery.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrentRun>

namespace litecode::workspace {
namespace {

constexpr qint64 maximumReplaceFileBytes = 32 * 1024 * 1024;

QString resolvedExistingPath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::fromNativeSeparators(
        QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical));
}

bool containsPath(const QString& rootPath, const QString& path) {
    const QString root = resolvedExistingPath(rootPath);
    const QString candidate = resolvedExistingPath(path);
    const Qt::CaseSensitivity sensitivity = core::fileSystemCaseSensitivity(root);
    return !root.isEmpty() && (candidate.compare(root, sensitivity) == 0 ||
                               candidate.startsWith(root + QLatin1Char('/'), sensitivity));
}

QString replacementFor(const QRegularExpressionMatch& match,
                       const WorkspaceReplaceRequest& request) {
    return expandWorkspaceReplacement(request.replacement, match.capturedTexts(),
                                      request.useRegularExpression, request.preserveCase,
                                      request.matchCase);
}

WorkspaceReplaceOutcome runReplace(const WorkspaceReplaceRequest& request,
                                   const std::shared_ptr<std::atomic_bool>& cancelled) {
    WorkspaceReplaceOutcome outcome;
    const QFileInfo workspaceInfo(request.workspaceRoot);
    const QFileInfo rootInfo(request.rootPath);
    if (!workspaceInfo.isDir() || !rootInfo.isDir() || request.query.isEmpty() ||
        !containsPath(workspaceInfo.absoluteFilePath(), rootInfo.absoluteFilePath())) {
        outcome.safeDiagnostic =
            QStringLiteral("The search root is unavailable or outside the active workspace.");
        return outcome;
    }
    const QRegularExpression expression(compileWorkspaceTextQuery(
        {request.query, request.matchCase, request.matchWholeWord, request.useRegularExpression}));
    if (!expression.isValid()) {
        outcome.safeDiagnostic = QStringLiteral("The search pattern is invalid.");
        return outcome;
    }

    const QDir workspace(resolvedExistingPath(workspaceInfo.absoluteFilePath()));
    const QDir root(resolvedExistingPath(rootInfo.absoluteFilePath()));
    const auto finalize = [&outcome] {
        if (outcome.skippedFiles > 0 && outcome.safeDiagnostic.isEmpty()) {
            outcome.safeDiagnostic =
                QStringLiteral("Replaced %1 occurrence(s) across %2 file(s), but skipped %3 "
                               "file(s).")
                    .arg(outcome.replacements)
                    .arg(outcome.changedFiles)
                    .arg(outcome.skippedFiles);
        }
        return outcome;
    };
    const auto replaceInFile = [&](const QString& requestedPath,
                                   const QVector<WorkspaceReplaceMatch>* selectedMatches =
                                       nullptr) {
        if (cancelled->load(std::memory_order_relaxed)) {
            outcome.cancelled = true;
            return false;
        }
        const QString absolutePath = QFileInfo(requestedPath).isAbsolute()
                                         ? requestedPath
                                         : root.absoluteFilePath(requestedPath);
        const QFileInfo fileInfo(absolutePath);
        if (!fileInfo.exists() || !fileInfo.isFile() ||
            !containsPath(workspace.absolutePath(), absolutePath) ||
            !containsPath(root.absolutePath(), absolutePath) ||
            isWorkspacePathIgnored(workspace.relativeFilePath(absolutePath)) ||
            fileInfo.size() > maximumReplaceFileBytes) {
            ++outcome.skippedFiles;
            return true;
        }
        const QString path = resolvedExistingPath(absolutePath);
        const QByteArray originalIdentity = core::fileSystemEntryIdentity(path);
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            ++outcome.skippedFiles;
            return true;
        }
        const QByteArray bytes = input.read(maximumReplaceFileBytes + 1);
        if (bytes.size() > maximumReplaceFileBytes || input.error() != QFileDevice::NoError ||
            !input.atEnd()) {
            ++outcome.skippedFiles;
            return true;
        }
        // QSaveFile replaces the destination atomically. On Windows that replacement can fail
        // while this read handle is still open, so release it before opening the save file.
        input.close();
        QByteArray utf8;
        core::TextEncoding actualEncoding{};
        bool hadByteOrderMark = false;
        if (!core::decodeText(bytes, request.encoding, true, &utf8, &actualEncoding, nullptr,
                              &hadByteOrderMark)) {
            ++outcome.skippedFiles;
            return true;
        }
        // A bulk edit must never normalize malformed or ambiguous byte sequences outside the
        // requested match. Only modify files whose original bytes round-trip exactly.
        QByteArray originalRoundTrip;
        if (!core::encodeText(utf8, actualEncoding, &originalRoundTrip, nullptr,
                              hadByteOrderMark) ||
            originalRoundTrip != bytes) {
            ++outcome.skippedFiles;
            return true;
        }
        const QString text = QString::fromUtf8(utf8);
        QHash<qsizetype, WorkspaceReplaceMatch> selectedPositions;
        if (selectedMatches) {
            QVector<qsizetype> lineStarts{0};
            for (qsizetype newline = text.indexOf(QLatin1Char('\n')); newline >= 0;
                 newline = text.indexOf(QLatin1Char('\n'), newline + 1))
                lineStarts.push_back(newline + 1);
            for (const WorkspaceReplaceMatch& target : *selectedMatches) {
                if (target.line <= 0 || target.line > lineStarts.size() || target.column <= 0 ||
                    target.length < 0) {
                    ++outcome.skippedFiles;
                    return true;
                }
                const qsizetype position = lineStarts.at(target.line - 1) + target.column - 1;
                if (position > text.size() || target.length > text.size() - position ||
                    text.mid(position, target.length) != target.matchedText ||
                    selectedPositions.contains(position)) {
                    ++outcome.skippedFiles;
                    return true;
                }
                selectedPositions.insert(position, target);
            }
        }
        QString updated;
        updated.reserve(text.size());
        int replacements = 0;
        qsizetype lineStart = 0;
        int lineNumber = 1;
        while (lineStart <= text.size()) {
            const qsizetype newline = text.indexOf(QLatin1Char('\n'), lineStart);
            const qsizetype lineEnd = newline >= 0 ? newline : text.size();
            const bool hasCarriageReturn =
                lineEnd > lineStart && text.at(lineEnd - 1) == QLatin1Char('\r');
            const QString line = text.mid(lineStart, lineEnd - lineStart - hasCarriageReturn);
            QRegularExpressionMatchIterator matches = expression.globalMatch(line);
            qsizetype offset = 0;
            while (matches.hasNext()) {
                const QRegularExpressionMatch match = matches.next();
                updated += line.mid(offset, match.capturedStart() - offset);
                const qsizetype position = lineStart + match.capturedStart();
                const int column = static_cast<int>(match.capturedStart()) + 1;
                const bool targetsLine =
                    request.targetLine <= 0 || lineNumber == request.targetLine;
                const bool targetsColumn =
                    request.targetColumn <= 0 || column == request.targetColumn;
                const bool targetsLength =
                    request.targetLength <= 0 || match.capturedLength() == request.targetLength;
                const bool mayReplace = !request.replaceFirstMatchOnly || replacements == 0;
                const auto selected = selectedPositions.constFind(position);
                const bool matchesSelection = selected != selectedPositions.cend() &&
                                              selected->length == match.capturedLength() &&
                                              selected->matchedText == match.captured();
                if (selectedMatches ? matchesSelection
                                    : targetsLine && targetsColumn && targetsLength && mayReplace) {
                    updated += replacementFor(match, request);
                    ++replacements;
                } else {
                    updated += match.captured();
                }
                offset = match.capturedEnd();
            }
            updated += line.mid(offset);
            if (hasCarriageReturn)
                updated += QLatin1Char('\r');
            if (newline < 0)
                break;
            updated += QLatin1Char('\n');
            lineStart = newline + 1;
            ++lineNumber;
        }
        if (selectedMatches && replacements != selectedMatches->size()) {
            ++outcome.skippedFiles;
            return true;
        }
        if (replacements == 0)
            return true;
        if (cancelled->load(std::memory_order_relaxed)) {
            outcome.cancelled = true;
            return false;
        }
        QByteArray encoded;
        const bool writeByteOrderMark = hadByteOrderMark ||
                                        actualEncoding == core::TextEncoding::Utf8Bom ||
                                        actualEncoding == core::TextEncoding::Utf16Le ||
                                        actualEncoding == core::TextEncoding::Utf16Be;
        if (!core::encodeText(updated.toUtf8(), actualEncoding, &encoded, nullptr,
                              writeByteOrderMark)) {
            ++outcome.skippedFiles;
            return true;
        }
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(encoded) != encoded.size()) {
            ++outcome.skippedFiles;
            return true;
        }
        // Check after preparing the replacement, immediately before the atomic commit.
        // A file changed while encoding or writing the temporary file must not be overwritten.
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly)) {
            ++outcome.skippedFiles;
            return true;
        }
        const QByteArray currentBytes = current.read(bytes.size() + 1);
        if (currentBytes != bytes || current.error() != QFileDevice::NoError || !current.atEnd()) {
            ++outcome.skippedFiles;
            return true;
        }
        current.close();
        if (!containsPath(workspace.absolutePath(), path) ||
            !containsPath(root.absolutePath(), path) ||
            (!originalIdentity.isEmpty() &&
             core::fileSystemEntryIdentity(path) != originalIdentity)) {
            ++outcome.skippedFiles;
            return true;
        }
        if (!output.commit()) {
            ++outcome.skippedFiles;
            return true;
        }
        outcome.replacements += replacements;
        ++outcome.changedFiles;
        return true;
    };

    if (!request.targetMatches.isEmpty()) {
        QHash<QString, QVector<WorkspaceReplaceMatch>> byFile;
        for (const WorkspaceReplaceMatch& match : request.targetMatches)
            byFile[match.filePath].push_back(match);
        for (auto it = byFile.cbegin(); it != byFile.cend(); ++it) {
            if (!replaceInFile(it.key(), &it.value()))
                return outcome;
        }
        return finalize();
    }
    if (!request.targetPath.isEmpty()) {
        replaceInFile(request.targetPath);
        return finalize();
    }
    if (!request.targetPaths.isEmpty()) {
        QSet<QString> uniquePaths(request.targetPaths.cbegin(), request.targetPaths.cend());
        for (const QString& path : uniquePaths) {
            if (!replaceInFile(path))
                return outcome;
        }
        return finalize();
    }
    QDirIterator iterator(root.absolutePath(),
                          QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System |
                              QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (!replaceInFile(iterator.next()))
            return outcome;
    }
    return finalize();
}

} // namespace

WorkspaceReplace::WorkspaceReplace(QObject* parent) : QObject(parent) {}

WorkspaceReplace::~WorkspaceReplace() { cancel(); }

bool WorkspaceReplace::replaceAll(WorkspaceReplaceRequest request) {
    if (isRunning())
        return false;
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = cancelled_;
    watcher_ = new QFutureWatcher<WorkspaceReplaceOutcome>(this);
    auto* watcher = watcher_;
    connect(watcher, &QFutureWatcher<WorkspaceReplaceOutcome>::finished, this, [this, watcher] {
        WorkspaceReplaceOutcome outcome = watcher->result();
        watcher_ = nullptr;
        cancelled_.reset();
        watcher->deleteLater();
        emit completed(outcome);
    });
    watcher->setFuture(QtConcurrent::run(runReplace, std::move(request), cancelled));
    return true;
}

void WorkspaceReplace::cancel() {
    if (cancelled_)
        cancelled_->store(true, std::memory_order_relaxed);
}

bool WorkspaceReplace::isRunning() const noexcept { return watcher_ != nullptr; }

} // namespace litecode::workspace
