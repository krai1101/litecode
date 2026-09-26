#include "workspace/WorkspaceSearch.h"
#include "core/TextEncoding.h"
#include "workspace/WorkspaceFileScope.h"
#include "workspace/WorkspaceTextQuery.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QtConcurrentRun>

#include <algorithm>
#include <set>
#include <utility>

namespace litecode::workspace {
namespace {

constexpr qint64 maximumRipgrepReadChunkBytes = 64 * 1024;
constexpr qsizetype maximumRipgrepEventBytes = 512 * 1024;

int fuzzyFileScore(const QString& relativePath, const QString& query) {
    if (query.isEmpty()) {
        return 0;
    }
    const QString path = relativePath.toLower();
    const QString needle = query.toLower();
    const QString fileName = QFileInfo(relativePath).fileName().toLower();
    if (fileName == needle) {
        return 10'000;
    }
    if (fileName.startsWith(needle)) {
        return 8'000 - static_cast<int>(fileName.size());
    }
    if (const qsizetype index = fileName.indexOf(needle); index >= 0) {
        return 6'000 - static_cast<int>(index * 10 + fileName.size());
    }
    if (const qsizetype index = path.indexOf(needle); index >= 0) {
        return 4'000 - static_cast<int>(index + path.size());
    }
    qsizetype queryIndex = 0;
    int gaps = 0;
    qsizetype previous = -1;
    for (qsizetype index = 0; index < path.size() && queryIndex < needle.size(); ++index) {
        if (path.at(index) == needle.at(queryIndex)) {
            if (previous >= 0) {
                gaps += static_cast<int>(index - previous - 1);
            }
            previous = index;
            ++queryIndex;
        }
    }
    return queryIndex == needle.size() ? 2'000 - gaps * 5 - static_cast<int>(path.size()) : -1;
}

struct BetterSearchResult final {
    bool operator()(const SearchResult& left, const SearchResult& right) const {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        const int insensitive = QString::compare(left.preview, right.preview, Qt::CaseInsensitive);
        if (insensitive != 0) {
            return insensitive < 0;
        }
        const int sensitive = QString::compare(left.preview, right.preview, Qt::CaseSensitive);
        if (sensitive != 0) {
            return sensitive < 0;
        }
        return QString::compare(left.filePath, right.filePath, Qt::CaseSensitive) < 0;
    }
};

void markTruncated(SearchOutcome* outcome, const QString& reason) {
    outcome->truncated = true;
    if (reason != QStringLiteral("Search reached its result limit."))
        outcome->countsComplete = false;
    if (outcome->safeDiagnostic.isEmpty()) {
        outcome->safeDiagnostic = reason;
    }
}

bool budgetExpired(const SearchBudgets& budgets, const QElapsedTimer& elapsed,
                   const WorkspaceSearch::CancellationToken& cancelled, SearchOutcome* outcome) {
    if (cancelled->load(std::memory_order_relaxed)) {
        outcome->completion = core::CompletionKind::Cancelled;
        return true;
    }
    if (elapsed.elapsed() >= budgets.maximumElapsedMs) {
        markTruncated(outcome, QStringLiteral("Search stopped at its time limit."));
        return true;
    }
    return false;
}

QString leftSearchPreviewContext(QString text) {
    // This mirrors VS Code's lcut(fullBefore, 26, '…'): leading whitespace is
    // always removed, then only the final 26 characters of long context remain.
    int leadingWhitespace = 0;
    while (leadingWhitespace < text.size() && text.at(leadingWhitespace).isSpace())
        ++leadingWhitespace;
    text.remove(0, leadingWhitespace);
    constexpr int maximumContextCharacters = 26;
    if (text.size() < maximumContextCharacters)
        return text;

    static const QRegularExpression wordBoundary(QStringLiteral("\\b"));
    int cutAt = 0;
    QRegularExpressionMatchIterator boundaries = wordBoundary.globalMatch(text);
    while (boundaries.hasNext()) {
        const int boundary = boundaries.next().capturedStart();
        if (text.size() - boundary < maximumContextCharacters)
            break;
        cutAt = boundary;
    }
    if (cutAt == 0)
        return text;
    while (cutAt < text.size() && text.at(cutAt).isSpace())
        ++cutAt;
    return QChar(0x2026) + text.mid(cutAt);
}

QPair<QString, int> previewForMatch(const QString& line, int matchStart, int matchLength) {
    // Match VS Code's MatchImpl.preview(): lcut the text before the hit, keep
    // the hit, and retain up to 250 characters total. The view supplies the
    // visual end ellipsis when the available row width is exhausted.
    constexpr int maximumPreviewCharacters = 250;
    const QString before = leftSearchPreviewContext(line.left(matchStart));
    QString inside = line.mid(matchStart, matchLength);
    QString after = line.mid(matchStart + matchLength);
    int remaining = maximumPreviewCharacters - before.size();
    inside = inside.left(qMax(0, remaining));
    remaining -= inside.size();
    after = after.left(qMax(0, remaining));
    return {before + inside + after, before.size()};
}

quint64 appendMatchingLine(const QByteArray& bytes, const SearchRequest& request,
                           const QString& path, int lineNumber, int maximumMatches,
                           QVector<SearchResult>* fileResults) {
    QString line = QString::fromUtf8(bytes);
    if (line.endsWith(QLatin1Char('\r'))) {
        line.chop(1);
    }
    const QRegularExpression expression(compileWorkspaceTextQuery(
        {request.query, request.matchCase, request.matchWholeWord, request.useRegularExpression}));
    if (!expression.isValid()) {
        return 0;
    }
    QRegularExpressionMatchIterator matches = expression.globalMatch(line);
    quint64 count = 0;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        ++count;
        if (fileResults->size() >= maximumMatches)
            continue;
        const int column = static_cast<int>(match.capturedStart()) + 1;
        const int length = static_cast<int>(match.capturedLength());
        const auto [preview, previewMatchStart] =
            previewForMatch(line, static_cast<int>(match.capturedStart()), length);
        fileResults->push_back({path, lineNumber, preview, 0, column, length, match.captured(),
                                previewMatchStart, match.capturedTexts()});
    }
    return count;
}

bool scanContentFile(const QString& path, const SearchRequest& request,
                     const SearchBudgets& budgets, const QElapsedTimer& elapsed,
                     const WorkspaceSearch::CancellationToken& cancelled, SearchOutcome* outcome) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        markTruncated(outcome, QStringLiteral("One or more files could not be read."));
        return true;
    }
    if (file.size() > budgets.maximumContentBytesPerFile) {
        markTruncated(outcome,
                      QStringLiteral("One or more files exceeded the content-read limit."));
        return true;
    }

    QByteArray bytes;
    while (!file.atEnd()) {
        if (budgetExpired(budgets, elapsed, cancelled, outcome))
            return false;
        const qint64 remaining = budgets.maximumContentBytesPerFile - bytes.size();
        qint64 readSize = std::min<qint64>(budgets.contentBlockBytes, remaining);
        if (readSize < budgets.contentBlockBytes)
            ++readSize;
        const QByteArray block = file.read(readSize);
        if (block.isEmpty()) {
            if (file.error() != QFileDevice::NoError)
                markTruncated(outcome, QStringLiteral("One or more files could not be read."));
            return true;
        }
        bytes += block;
        if (bytes.size() > budgets.maximumContentBytesPerFile) {
            markTruncated(outcome,
                          QStringLiteral("One or more files exceeded the content-read limit."));
            return true;
        }
    }

    QByteArray utf8;
    core::TextEncoding actualEncoding{};
    if (!core::decodeText(bytes, request.encoding, true, &utf8, &actualEncoding))
        return true;

    QVector<SearchResult> fileResults;
    bool fileMatched = false;
    qsizetype cursor = 0;
    int lineNumber = 1;
    while (cursor <= utf8.size()) {
        if (budgetExpired(budgets, elapsed, cancelled, outcome))
            return false;
        const qsizetype newline = utf8.indexOf('\n', cursor);
        const qsizetype end = newline >= 0 ? newline : utf8.size();
        QByteArray line = utf8.mid(cursor, end - cursor);
        if (line.size() > budgets.maximumLineBytes) {
            line.truncate(budgets.maximumLineBytes);
            markTruncated(outcome,
                          QStringLiteral("One or more oversized text lines were clipped."));
        }
        const quint64 lineMatches =
            appendMatchingLine(line, request, path, lineNumber,
                               request.maximumResults - outcome->results.size(), &fileResults);
        outcome->totalMatches += lineMatches;
        fileMatched = fileMatched || lineMatches > 0;
        if (outcome->totalMatches > static_cast<quint64>(request.maximumResults))
            markTruncated(outcome, QStringLiteral("Search reached its result limit."));
        if (newline < 0)
            break;
        cursor = newline + 1;
        ++lineNumber;
    }

    const int available = request.maximumResults - outcome->results.size();
    outcome->results += fileResults.sliced(0, std::min<qsizetype>(available, fileResults.size()));
    if (fileMatched)
        ++outcome->totalFiles;
    return true;
}

SearchOutcome runSearch(const SearchRequest& request, const SearchBudgets& budgets,
                        const WorkspaceSearch::CancellationToken& cancelled) {
    SearchOutcome outcome;
    outcome.operationId = request.operationId;
    const QFileInfo rootInfo(request.rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        outcome.completion = core::CompletionKind::Failed;
        outcome.safeDiagnostic = QStringLiteral("The search root is unavailable.");
        return outcome;
    }
    if (!request.filesOnly && request.query.isEmpty()) {
        return outcome;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    const QDir root(rootInfo.absoluteFilePath());
    QVector<QString> directories{root.absolutePath()};
    std::multiset<SearchResult, BetterSearchResult> rankedFiles;
    const QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden |
                                  QDir::System | QDir::NoSymLinks;

    bool stop = false;
    while (!directories.isEmpty() && !stop) {
        const QString directory = directories.takeLast();
        QDirIterator entries(directory, filters, QDirIterator::NoIteratorFlags);
        while (entries.hasNext()) {
            if (outcome.visitedEntries >= budgets.maximumVisitedEntries) {
                markTruncated(&outcome, QStringLiteral("Search reached its traversal limit."));
                stop = true;
                break;
            }
            if (outcome.visitedEntries % static_cast<quint64>(budgets.cancellationCheckInterval) ==
                    0 &&
                budgetExpired(budgets, elapsed, cancelled, &outcome)) {
                stop = true;
                break;
            }

            const QString path = entries.next();
            ++outcome.visitedEntries;
            const QFileInfo info = entries.fileInfo();
            const QString relative = root.relativeFilePath(path);
            if (isWorkspacePathIgnored(relative)) {
                continue;
            }
            if (info.isDir()) {
                directories.push_back(path);
                continue;
            }
            if (!info.isFile()) {
                continue;
            }

            if (request.filesOnly) {
                const int score = fuzzyFileScore(relative, request.query);
                if (score < 0) {
                    continue;
                }
                rankedFiles.insert({path, 0, relative, score});
                if (rankedFiles.size() > static_cast<size_t>(request.maximumResults)) {
                    rankedFiles.erase(std::prev(rankedFiles.end()));
                }
                outcome.peakRetainedCandidates =
                    std::max(outcome.peakRetainedCandidates, static_cast<int>(rankedFiles.size()));
            } else if (!scanContentFile(path, request, budgets, elapsed, cancelled, &outcome)) {
                stop = true;
                break;
            }
        }
    }

    if (cancelled->load(std::memory_order_relaxed)) {
        outcome.completion = core::CompletionKind::Cancelled;
        outcome.results.clear();
    } else if (request.filesOnly) {
        outcome.results.reserve(static_cast<qsizetype>(rankedFiles.size()));
        for (const SearchResult& result : rankedFiles) {
            outcome.results.push_back(result);
        }
    }
    return outcome;
}

QString bundledRipgrepPath() {
    const QString binary = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("rg")
#ifdef Q_OS_WIN
                                         + QStringLiteral(".exe")
#endif
                               );
    return QFileInfo(binary).isExecutable() ? binary : QString{};
}

struct RipgrepMatches final {
    QString filePath;
    quint64 count{0};
    QVector<SearchResult> retained;
};

RipgrepMatches parseRipgrepMatches(const QByteArray& line, int maximumRetained) {
    RipgrepMatches parsed;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return parsed;
    const QJsonObject event = document.object();
    if (event.value(QStringLiteral("type")).toString() != QStringLiteral("match"))
        return parsed;
    const QJsonObject data = event.value(QStringLiteral("data")).toObject();
    const QJsonObject path = data.value(QStringLiteral("path")).toObject();
    const QJsonObject lines = data.value(QStringLiteral("lines")).toObject();
    const QString filePath = path.value(QStringLiteral("text")).toString();
    if (filePath.isEmpty())
        return parsed;
    parsed.filePath = filePath;
    const QByteArray lineBytes = lines.value(QStringLiteral("text")).toString().toUtf8();
    QString lineText = lines.value(QStringLiteral("text")).toString();
    while (lineText.endsWith(QLatin1Char('\n')) || lineText.endsWith(QLatin1Char('\r')))
        lineText.chop(1);
    const QJsonArray submatches = data.value(QStringLiteral("submatches")).toArray();
    for (const QJsonValue& value : submatches) {
        const QJsonObject submatch = value.toObject();
        const int start = submatch.value(QStringLiteral("start")).toInt(-1);
        const int end = submatch.value(QStringLiteral("end")).toInt(-1);
        if (start < 0 || end < start || end > lineBytes.size())
            continue;
        ++parsed.count;
        if (parsed.retained.size() >= maximumRetained)
            continue;
        const int column = static_cast<int>(QString::fromUtf8(lineBytes.left(start)).size());
        const int length =
            static_cast<int>(QString::fromUtf8(lineBytes.mid(start, end - start)).size());
        const auto [preview, previewMatchStart] = previewForMatch(lineText, column, length);
        parsed.retained.push_back({filePath,
                                   data.value(QStringLiteral("line_number")).toInt(),
                                   preview,
                                   0,
                                   column + 1,
                                   length,
                                   QString::fromUtf8(lineBytes.mid(start, end - start)),
                                   previewMatchStart,
                                   {QString::fromUtf8(lineBytes.mid(start, end - start))}});
    }
    return parsed;
}

} // namespace

WorkspaceSearch::WorkspaceSearch(QObject* parent, SearchRunner runner, SearchBudgets budgets,
                                 QString ripgrepExecutable)
    : QObject(parent), runner_(runner ? std::move(runner) : SearchRunner{runSearch}),
      budgets_(budgets), ripgrepExecutable_(std::move(ripgrepExecutable)) {
    budgets_.maximumVisitedEntries = std::max<quint64>(1, budgets_.maximumVisitedEntries);
    budgets_.maximumContentBytesPerFile = std::max<qint64>(1, budgets_.maximumContentBytesPerFile);
    budgets_.maximumLineBytes = std::max<qsizetype>(1, budgets_.maximumLineBytes);
    budgets_.contentBlockBytes = std::max<qsizetype>(1, budgets_.contentBlockBytes);
    budgets_.maximumResults = std::max(1, budgets_.maximumResults);
    budgets_.maximumElapsedMs = std::max(1, budgets_.maximumElapsedMs);
    budgets_.cancellationCheckInterval = std::max(1, budgets_.cancellationCheckInterval);
}

WorkspaceSearch::~WorkspaceSearch() {
    cancel();
    if (watcher_) {
        disconnect(watcher_, nullptr, this, nullptr);
    }
    if (ripgrepProcess_) {
        disconnect(ripgrepProcess_, nullptr, this, nullptr);
    }
}

core::OperationId WorkspaceSearch::searchText(const QString& rootPath, const QString& query,
                                              int maximumResults, bool matchCase,
                                              bool matchWholeWord, bool useRegularExpression,
                                              core::TextEncoding encoding) {
    return start(rootPath, query, maximumResults, false, matchCase, matchWholeWord,
                 useRegularExpression, encoding);
}

core::OperationId WorkspaceSearch::findFiles(const QString& rootPath, const QString& query,
                                             int maximumResults) {
    return start(rootPath, query, maximumResults, true);
}

void WorkspaceSearch::cancel() {
    if (cancellation_) {
        cancellation_->store(true, std::memory_order_relaxed);
    }
    if (ripgrepProcess_) {
        ripgrepProcess_->kill();
    }
    if (pendingRequest_) {
        const SearchRequest pending = *pendingRequest_;
        pendingRequest_.reset();
        completeWithoutWorker(pending, core::CompletionKind::Cancelled);
    }
}

int WorkspaceSearch::activeWorkerCount() const noexcept {
    return watcher_ || ripgrepProcess_ ? 1 : 0;
}

int WorkspaceSearch::pendingRequestCount() const noexcept { return pendingRequest_ ? 1 : 0; }

core::OperationId WorkspaceSearch::start(const QString& rootPath, const QString& query,
                                         int maximumResults, bool filesOnly, bool matchCase,
                                         bool matchWholeWord, bool useRegularExpression,
                                         core::TextEncoding encoding) {
    SearchRequest request;
    request.operationId = operationIds_.next();
    request.rootPath = rootPath;
    request.query = query;
    request.maximumResults = std::clamp(maximumResults, 1, budgets_.maximumResults);
    request.filesOnly = filesOnly;
    request.matchCase = matchCase;
    request.matchWholeWord = matchWholeWord;
    request.useRegularExpression = useRegularExpression;
    request.encoding = encoding;
    latestRequest_ = request.operationId;

    if (watcher_ || ripgrepProcess_) {
        cancellation_->store(true, std::memory_order_relaxed);
        if (ripgrepProcess_)
            ripgrepProcess_->kill();
        if (pendingRequest_) {
            completeWithoutWorker(*pendingRequest_, core::CompletionKind::Cancelled);
        }
        pendingRequest_ = request;
    } else {
        launch(request);
    }
    return request.operationId;
}

void WorkspaceSearch::launch(const SearchRequest& request) {
    currentRequest_ = request;
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    // Regex and word boundaries must use the same engine as WorkspaceReplace so the
    // displayed matches are exactly the positions that replacement can apply.
    if (!request.filesOnly && !request.useRegularExpression && !request.matchWholeWord &&
        launchRipgrep(request))
        return;
    launchFallback(request);
}

void WorkspaceSearch::launchFallback(const SearchRequest& request) {
    const CancellationToken token = cancellation_;
    watcher_ = new QFutureWatcher<SearchOutcome>(this);
    QFutureWatcher<SearchOutcome>* const watcher = watcher_;
    connect(watcher, &QFutureWatcher<SearchOutcome>::finished, this,
            [this, watcher, token, request] {
                SearchOutcome outcome = watcher->result();
                if (token->load(std::memory_order_relaxed)) {
                    outcome.completion = core::CompletionKind::Cancelled;
                    outcome.results.clear();
                }
                watcher_ = nullptr;
                cancellation_.reset();
                currentRequest_.reset();
                watcher->deleteLater();
                publish(std::move(outcome), request.operationId == latestRequest_);

                if (pendingRequest_) {
                    const SearchRequest pending = *pendingRequest_;
                    pendingRequest_.reset();
                    launch(pending);
                }
            });
    watcher->setFuture(QtConcurrent::run(runner_, request, budgets_, token));
}

bool WorkspaceSearch::launchRipgrep(const SearchRequest& request) {
    const QString executable =
        ripgrepExecutable_.isEmpty() ? bundledRipgrepPath() : ripgrepExecutable_;
    if (executable.isEmpty())
        return false;

    auto* process = new QProcess(this);
    ripgrepProcess_ = process;
    ripgrepPendingOutput_.clear();
    ripgrepResults_.clear();
    ripgrepTotalMatches_ = 0;
    ripgrepMatchedPaths_.clear();
    ripgrepTruncated_ = false;
    ripgrepTruncationReason_.clear();

    QStringList arguments{QStringLiteral("--json"),          QStringLiteral("--no-messages"),
                          QStringLiteral("--hidden"),        QStringLiteral("--iglob"),
                          QStringLiteral("!.git/**"),        QStringLiteral("--iglob"),
                          QStringLiteral("!build/**"),       QStringLiteral("--iglob"),
                          QStringLiteral("!node_modules/**")};
    arguments << QStringLiteral("--max-filesize")
              << QString::number(budgets_.maximumContentBytesPerFile);
    if (!request.matchCase)
        arguments << QStringLiteral("-i");
    if (request.matchWholeWord)
        arguments << QStringLiteral("-w");
    if (!request.useRegularExpression)
        arguments << QStringLiteral("-F");
    if (request.encoding != core::TextEncoding::Utf8 &&
        request.encoding != core::TextEncoding::Utf8Bom) {
        arguments << QStringLiteral("--encoding") << core::ripgrepEncodingName(request.encoding);
    }
    arguments << QStringLiteral("--") << request.query << request.rootPath;

    process->setProgram(executable);
    process->setArguments(arguments);
    process->setWorkingDirectory(request.rootPath);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, request] {
        if (process != ripgrepProcess_)
            return;
        consumeRipgrepOutput(process, request);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, request](int exitCode, QProcess::ExitStatus exitStatus) {
                if (process != ripgrepProcess_)
                    return;
                finishRipgrep(request, exitCode, exitStatus == QProcess::CrashExit);
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, request](QProcess::ProcessError error) {
                if (process != ripgrepProcess_ || error != QProcess::FailedToStart)
                    return;
                ripgrepProcess_ = nullptr;
                process->deleteLater();
                launchFallback(request);
            });
    process->start();
    return true;
}

void WorkspaceSearch::consumeRipgrepOutput(QProcess* process, const SearchRequest& request) {
    while (process->bytesAvailable() > 0) {
        const qint64 bytes = std::min(process->bytesAvailable(), maximumRipgrepReadChunkBytes);
        ripgrepPendingOutput_ += process->read(bytes);
        if (ripgrepPendingOutput_.size() > maximumRipgrepEventBytes) {
            ripgrepTruncated_ = true;
            ripgrepTruncationReason_ =
                QStringLiteral("One or more oversized result lines were skipped.");
            ripgrepPendingOutput_.clear();
            process->kill();
            return;
        }
        while (true) {
            const qsizetype end = ripgrepPendingOutput_.indexOf('\n');
            if (end < 0)
                break;
            const QByteArray event = ripgrepPendingOutput_.left(end);
            ripgrepPendingOutput_.remove(0, end + 1);
            RipgrepMatches parsed =
                parseRipgrepMatches(event, request.maximumResults - ripgrepResults_.size());
            ripgrepTotalMatches_ += parsed.count;
            if (parsed.count > 0)
                ripgrepMatchedPaths_.insert(parsed.filePath);
            ripgrepResults_ += std::move(parsed.retained);
        }
    }
}

void WorkspaceSearch::finishRipgrep(const SearchRequest& request, int exitCode, bool crashed) {
    QProcess* const process = ripgrepProcess_;
    if (process) {
        consumeRipgrepOutput(process, request);
        RipgrepMatches trailing = parseRipgrepMatches(
            ripgrepPendingOutput_, request.maximumResults - ripgrepResults_.size());
        ripgrepTotalMatches_ += trailing.count;
        if (trailing.count > 0)
            ripgrepMatchedPaths_.insert(trailing.filePath);
        ripgrepResults_ += std::move(trailing.retained);
        process->deleteLater();
    }
    ripgrepProcess_ = nullptr;

    const bool cancelled = cancellation_ && cancellation_->load(std::memory_order_relaxed);
    if (!cancelled && !ripgrepTruncated_ && (crashed || exitCode > 1)) {
        ripgrepPendingOutput_.clear();
        ripgrepResults_.clear();
        launchFallback(request);
        return;
    }

    SearchOutcome outcome;
    outcome.operationId = request.operationId;
    outcome.results = std::move(ripgrepResults_);
    outcome.totalMatches = ripgrepTotalMatches_;
    outcome.totalFiles = ripgrepMatchedPaths_.size();
    outcome.countsComplete = !ripgrepTruncated_;
    outcome.truncated =
        ripgrepTruncated_ || outcome.totalMatches > static_cast<quint64>(outcome.results.size());
    outcome.peakRetainedCandidates = outcome.results.size();
    if (ripgrepTruncated_)
        outcome.safeDiagnostic = ripgrepTruncationReason_;
    else if (outcome.truncated)
        outcome.safeDiagnostic = QStringLiteral("Search reached its result limit.");
    if (cancelled) {
        outcome.completion = core::CompletionKind::Cancelled;
        outcome.results.clear();
    }
    ripgrepPendingOutput_.clear();
    ripgrepTotalMatches_ = 0;
    ripgrepMatchedPaths_.clear();
    ripgrepTruncated_ = false;
    ripgrepTruncationReason_.clear();
    cancellation_.reset();
    currentRequest_.reset();
    publish(std::move(outcome), request.operationId == latestRequest_);
    if (pendingRequest_) {
        const SearchRequest pending = *pendingRequest_;
        pendingRequest_.reset();
        launch(pending);
    }
}

void WorkspaceSearch::completeWithoutWorker(const SearchRequest& request,
                                            core::CompletionKind completion) {
    SearchOutcome outcome;
    outcome.operationId = request.operationId;
    outcome.completion = completion;
    publish(std::move(outcome), request.operationId == latestRequest_);
}

void WorkspaceSearch::publish(SearchOutcome outcome, bool visible) {
    if (visible && outcome.completion == core::CompletionKind::Succeeded) {
        const SearchResultBatch batch{outcome.operationId, outcome.results};
        emit resultBatchReady(batch);
    }
    emit operationCompleted(outcome);
}

} // namespace litecode::workspace
