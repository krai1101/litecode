#pragma once

#include "core/DocumentTypes.h"
#include "core/OperationResult.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

template <typename T> class QFutureWatcher;
class QProcess;

namespace litecode::workspace {

inline constexpr int defaultMaximumTextResults = 500;

struct SearchResult final {
    QString filePath;
    int line{0};
    QString preview;
    int score{0};
    // One-based UTF-16 coordinates identify a single match for replace-in-results.
    int column{0};
    int length{0};
    QString matchedText;
    // Zero-based UTF-16 offset of this specific hit within preview.
    int previewMatchStart{-1};
    QStringList captures;
};

struct SearchBudgets final {
    quint64 maximumVisitedEntries{200'000};
    qint64 maximumContentBytesPerFile{8 * 1024 * 1024};
    qsizetype maximumLineBytes{256 * 1024};
    qsizetype contentBlockBytes{64 * 1024};
    int maximumResults{defaultMaximumTextResults};
    int maximumElapsedMs{15'000};
    int cancellationCheckInterval{64};
};

struct SearchRequest final {
    core::OperationId operationId;
    QString rootPath;
    QString query;
    int maximumResults{defaultMaximumTextResults};
    bool filesOnly{false};
    bool matchCase{false};
    bool matchWholeWord{false};
    bool useRegularExpression{false};
    core::TextEncoding encoding{core::TextEncoding::Utf8};
};

struct SearchOutcome final {
    core::OperationId operationId;
    core::CompletionKind completion{core::CompletionKind::Succeeded};
    QVector<SearchResult> results;
    quint64 totalMatches{0};
    int totalFiles{0};
    bool countsComplete{true};
    bool truncated{false};
    QString safeDiagnostic;
    quint64 visitedEntries{0};
    int peakRetainedCandidates{0};
};

struct SearchResultBatch final {
    core::OperationId operationId;
    QVector<SearchResult> results;
};

class WorkspaceSearch final : public QObject {
    Q_OBJECT

  public:
    using CancellationToken = std::shared_ptr<std::atomic_bool>;
    using SearchRunner = std::function<SearchOutcome(const SearchRequest&, const SearchBudgets&,
                                                     const CancellationToken&)>;

    explicit WorkspaceSearch(QObject* parent = nullptr, SearchRunner runner = {},
                             SearchBudgets budgets = {}, QString ripgrepExecutable = {});
    ~WorkspaceSearch() override;

    core::OperationId searchText(const QString& rootPath, const QString& query,
                                 int maximumResults = defaultMaximumTextResults,
                                 bool matchCase = false, bool matchWholeWord = false,
                                 bool useRegularExpression = false,
                                 core::TextEncoding encoding = core::TextEncoding::Utf8);
    core::OperationId findFiles(const QString& rootPath, const QString& query,
                                int maximumResults = 500);
    void cancel();

    [[nodiscard]] int activeWorkerCount() const noexcept;
    [[nodiscard]] int pendingRequestCount() const noexcept;
    [[nodiscard]] static constexpr int maximumOutstandingResultBatches() noexcept { return 1; }

  signals:
    // Every returned operation ID produces exactly one completion. Only the newest visible
    // request publishes a result batch.
    void resultBatchReady(const litecode::workspace::SearchResultBatch& batch);
    void operationCompleted(const litecode::workspace::SearchOutcome& outcome);

  private:
    core::OperationId start(const QString& rootPath, const QString& query, int maximumResults,
                            bool filesOnly, bool matchCase = false, bool matchWholeWord = false,
                            bool useRegularExpression = false,
                            core::TextEncoding encoding = core::TextEncoding::Utf8);
    void launch(const SearchRequest& request);
    void launchFallback(const SearchRequest& request);
    bool launchRipgrep(const SearchRequest& request);
    void consumeRipgrepOutput(QProcess* process, const SearchRequest& request);
    void finishRipgrep(const SearchRequest& request, int exitCode, bool crashed);
    void completeWithoutWorker(const SearchRequest& request, core::CompletionKind completion);
    void publish(SearchOutcome outcome, bool visible);

    QFutureWatcher<SearchOutcome>* watcher_{};
    QProcess* ripgrepProcess_{};
    QByteArray ripgrepPendingOutput_;
    QVector<SearchResult> ripgrepResults_;
    quint64 ripgrepTotalMatches_{};
    QSet<QString> ripgrepMatchedPaths_;
    bool ripgrepTruncated_{};
    QString ripgrepTruncationReason_;
    CancellationToken cancellation_;
    std::optional<SearchRequest> currentRequest_;
    std::optional<SearchRequest> pendingRequest_;
    core::OperationId latestRequest_;
    core::OperationIdGenerator operationIds_;
    SearchRunner runner_;
    SearchBudgets budgets_;
    QString ripgrepExecutable_;
};

} // namespace litecode::workspace

Q_DECLARE_METATYPE(litecode::workspace::SearchResult)
Q_DECLARE_METATYPE(litecode::workspace::SearchOutcome)
Q_DECLARE_METATYPE(litecode::workspace::SearchResultBatch)
