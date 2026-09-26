#include "editor/EditorBackend.h"
#include "editor/ScintillaEditor.h"
#include "ui/EditorArea.h"
#include "workspace/WorkspaceSearch.h"

#include <ScintillaEditBase.h>
#include <ScintillaMessages.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <optional>

class EditorBenchmark final : public QObject {
    Q_OBJECT

  private slots:
    void opensDeterministicFiveMegabyteFile();
    void incrementalEditReportsOnlyChangedBytes_data();
    void incrementalEditReportsOnlyChangedBytes();
    void searchesDeterministicLargeWorkspace();
};

void EditorBenchmark::opensDeterministicFiveMegabyteFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    constexpr qsizetype fixtureSize = 5 * 1024 * 1024;
    QByteArray line("int benchmark_value = 12345; // deterministic fixture\n");
    QByteArray fixture;
    fixture.reserve(fixtureSize);
    while (fixture.size() < fixtureSize) {
        fixture.append(line.first(std::min(line.size(), fixtureSize - fixture.size())));
    }
    QCOMPARE(fixture.size(), fixtureSize);
    const QString path = directory.filePath(QStringLiteral("five-megabytes.cpp"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(fixture), fixtureSize);
    file.close();

    litecode::ui::EditorArea area;
    bool opened = false;
    QByteArray loadedContents;
    connect(&area, &litecode::ui::EditorArea::documentOpened, &area,
            [&opened, &loadedContents](litecode::core::DocumentId, const QString&,
                                       const QByteArray& contents, qint64,
                                       litecode::core::DocumentStorageMode) {
                loadedContents = contents;
                opened = true;
            });
    QElapsedTimer timer;
    timer.start();
    QString error;
    QVERIFY2(area.openFile(path, &error), qPrintable(error));
    const qint64 callerElapsedMs = timer.elapsed();
    QTRY_VERIFY_WITH_TIMEOUT(opened, 10000);
    QCOMPARE(loadedContents, fixture);
    const qint64 completedElapsedMs = timer.elapsed();
    qInfo().noquote() << QStringLiteral(
                             "benchmark=editor_open_5mb caller_ms=%1 completed_ms=%2 bytes=%3 "
                             "os=%4 arch=%5")
                             .arg(callerElapsedMs)
                             .arg(completedElapsedMs)
                             .arg(fixtureSize)
                             .arg(QSysInfo::prettyProductName(),
                                  QSysInfo::currentCpuArchitecture());
}

void EditorBenchmark::incrementalEditReportsOnlyChangedBytes_data() {
    QTest::addColumn<int>("sizeMiB");
    QTest::newRow("1MiB") << 1;
    QTest::newRow("5MiB") << 5;
    QTest::newRow("20MiB") << 20;
}

void EditorBenchmark::incrementalEditReportsOnlyChangedBytes() {
    QFETCH(int, sizeMiB);
    const qsizetype fixtureSize = static_cast<qsizetype>(sizeMiB) * 1024 * 1024;
    const QByteArray line("int benchmark_value = 12345; // deterministic fixture\n");
    QByteArray fixture;
    fixture.reserve(fixtureSize);
    while (fixture.size() < fixtureSize)
        fixture.append(line.first(std::min(line.size(), fixtureSize - fixture.size())));

    litecode::editor::ScintillaEditor editor;
    editor.setFilePath(QStringLiteral("benchmark.cpp"));
    editor.setText(fixture);
    qint64 changedPosition = -1;
    qint64 removedLength = -1;
    QByteArray insertedText;
    connect(&editor, &litecode::editor::EditorBackend::textEdited, &editor,
            [&](qint64 position, qint64 removed, const QByteArray& inserted,
                const litecode::core::TextRange&) {
                changedPosition = position;
                removedLength = removed;
                insertedText = inserted;
            });
    auto* control = editor.findChild<ScintillaEditBase*>(QStringLiteral("scintillaControl"));
    QVERIFY(control != nullptr);
    const qint64 insertionPosition = fixtureSize / 2;
    QElapsedTimer timer;
    timer.start();
    control->sends(static_cast<unsigned int>(Scintilla::Message::InsertText),
                   static_cast<Scintilla::uptr_t>(insertionPosition), "x");
    const qint64 elapsedMs = timer.nsecsElapsed() / 1000000;

    QCOMPARE(changedPosition, insertionPosition);
    QCOMPARE(removedLength, 0);
    QCOMPARE(insertedText, QByteArray("x"));
    qInfo().noquote() << QStringLiteral(
                             "benchmark=editor_incremental_edit size_mib=%1 elapsed_ms=%2 "
                             "reported_insert_bytes=%3")
                             .arg(sizeMiB)
                             .arg(elapsedMs)
                             .arg(insertedText.size());
}

void EditorBenchmark::searchesDeterministicLargeWorkspace() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    constexpr int directoryCount = 100;
    constexpr int filesPerDirectory = 20;
    constexpr int fileCount = directoryCount * filesPerDirectory;
    for (int directoryIndex = 0; directoryIndex < directoryCount; ++directoryIndex) {
        const QString relativeDirectory =
            QStringLiteral("module-%1").arg(directoryIndex, 3, 10, QLatin1Char('0'));
        QVERIFY(root.mkdir(relativeDirectory));
        for (int fileIndex = 0; fileIndex < filesPerDirectory; ++fileIndex) {
            const bool target = fileIndex == 0;
            const QString fileName =
                target ? QStringLiteral("target_component_%1.cpp")
                             .arg(directoryIndex, 3, 10, QLatin1Char('0'))
                       : QStringLiteral("source_%1.cpp").arg(fileIndex, 2, 10, QLatin1Char('0'));
            QFile file(root.filePath(relativeDirectory + QLatin1Char('/') + fileName));
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray content = target ? QByteArray("int deterministic_search_token = 1;\n")
                                              : QByteArray("int ordinary_fixture_value = 0;\n");
            QCOMPARE(file.write(content), content.size());
        }
    }

    litecode::workspace::SearchBudgets budgets;
    budgets.maximumVisitedEntries = fileCount + directoryCount + 10;
    budgets.maximumResults = 50;
    litecode::workspace::WorkspaceSearch search(nullptr, {}, budgets);
    std::optional<litecode::workspace::SearchOutcome> completed;
    connect(&search, &litecode::workspace::WorkspaceSearch::operationCompleted, &search,
            [&completed](const auto& outcome) { completed = outcome; });

    QElapsedTimer timer;
    timer.start();
    search.findFiles(directory.path(), QStringLiteral("target_component"), 50);
    QTRY_VERIFY_WITH_TIMEOUT(completed.has_value(), 15000);
    const qint64 quickOpenElapsedMs = timer.elapsed();
    QCOMPARE(completed->completion, litecode::core::CompletionKind::Succeeded);
    QCOMPARE(completed->results.size(), 50);
    QCOMPARE(completed->peakRetainedCandidates, 50);
    QVERIFY(completed->visitedEntries <= budgets.maximumVisitedEntries);

    completed.reset();
    timer.restart();
    search.searchText(directory.path(), QStringLiteral("deterministic_search_token"), 50);
    QTRY_VERIFY_WITH_TIMEOUT(completed.has_value(), 15000);
    const qint64 contentSearchElapsedMs = timer.elapsed();
    QCOMPARE(completed->completion, litecode::core::CompletionKind::Succeeded);
    QCOMPARE(completed->results.size(), 50);
    QVERIFY(completed->truncated);
    QVERIFY(completed->visitedEntries <= budgets.maximumVisitedEntries);

    qInfo().noquote() << QStringLiteral(
                             "benchmark=workspace_search files=%1 quick_open_ms=%2 "
                             "content_search_ms=%3 retained_candidates=%4 result_limit=%5 os=%6 "
                             "arch=%7")
                             .arg(fileCount)
                             .arg(quickOpenElapsedMs)
                             .arg(contentSearchElapsedMs)
                             .arg(50)
                             .arg(budgets.maximumResults)
                             .arg(QSysInfo::prettyProductName(),
                                  QSysInfo::currentCpuArchitecture());
}

QTEST_MAIN(EditorBenchmark)
#include "tst_editor_benchmark.moc"
