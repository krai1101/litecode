#pragma once

#include "core/DocumentTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

template <typename T> class QFutureWatcher;

namespace litecode::workspace {

struct WorkspaceReplaceMatch final {
    QString filePath;
    int line{};
    int column{};
    int length{};
    QString matchedText;
};

struct WorkspaceReplaceRequest final {
    QString workspaceRoot;
    QString rootPath;
    QString query;
    QString replacement;
    bool matchCase{};
    bool matchWholeWord{};
    bool useRegularExpression{};
    bool preserveCase{};
    core::TextEncoding encoding{core::TextEncoding::Utf8};
    QString targetPath;
    int targetLine{};
    int targetColumn{};
    int targetLength{};
    bool replaceFirstMatchOnly{};
    QStringList targetPaths;
    QVector<WorkspaceReplaceMatch> targetMatches;
};

struct WorkspaceReplaceOutcome final {
    int replacements{};
    int changedFiles{};
    int skippedFiles{};
    QString safeDiagnostic;
    bool cancelled{};
};

class WorkspaceReplace final : public QObject {
    Q_OBJECT

  public:
    explicit WorkspaceReplace(QObject* parent = nullptr);
    ~WorkspaceReplace() override;

    bool replaceAll(WorkspaceReplaceRequest request);
    void cancel();
    [[nodiscard]] bool isRunning() const noexcept;

  signals:
    void completed(const litecode::workspace::WorkspaceReplaceOutcome& outcome);

  private:
    QFutureWatcher<WorkspaceReplaceOutcome>* watcher_{};
    std::shared_ptr<std::atomic_bool> cancelled_;
};

} // namespace litecode::workspace

Q_DECLARE_METATYPE(litecode::workspace::WorkspaceReplaceOutcome)
