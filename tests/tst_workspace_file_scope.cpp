#include "core/FileSystemPath.h"
#include "core/TextEncoding.h"
#include "workspace/WorkspaceFileScope.h"
#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"
#include "workspace/WorkspaceTextQuery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace litecode::workspace {

class WorkspaceFileScopeTest final : public QObject {
    Q_OBJECT

  private slots:
    void ignoresBuiltInDirectories();
    void normalizesMissingPathsThroughSymlinkedParent();
    void retainsSimilarNamesAndNormalFiles();
    void compilesSharedTextQuery();
    void replaceRejectsFilesOutsideWorkspace();
    void rejectsCaseDistinctSiblingOnSensitiveVolume();
    void replaceSkipsMalformedUtf8WithoutChangingBytes();
    void replaceReportsPartialFailure();
    void searchesUtf16UsingConfiguredEncoding();
    void searchesConfiguredEncodingWithRipgrep();
    void searchEnginesEnforceContentReadLimit();
    void workspaceTextSearchCapsAtFiveHundred();
    void replacePreservesUtf16Encoding();
    void replaceAddsBomToBomlessUtf16();
    void replaceAppliesConfiguredUtf8Bom();
    void handlesWindows1252SearchAndReplace();
    void handlesBuiltInCodePageSearchAndReplace();
    void replacesOnlyRecordedMatches();
    void replacesLineAnchoredMatches();
    void rejectsStaleRecordedMatch();
    void formatsPreviewWhitespaceAndLocatesEachMatch();
};

void WorkspaceFileScopeTest::normalizesMissingPathsThroughSymlinkedParent() {
#ifndef Q_OS_UNIX
    QSKIP("Directory symlink setup is platform-specific.");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString actual = temporary.filePath(QStringLiteral("actual"));
    const QString alias = temporary.filePath(QStringLiteral("alias"));
    QVERIFY(QDir().mkpath(actual));
    QVERIFY(QFile::link(actual, alias));
    const QString missing = QStringLiteral("renamed/document.txt");
    const QString actualPath = QDir(actual).filePath(missing);
    const QString aliasPath = QDir(alias).filePath(missing);
    QCOMPARE(core::normalizedFileSystemPath(aliasPath), core::normalizedFileSystemPath(actualPath));
    QVERIFY(core::pathsReferToSameEntry(aliasPath, actualPath));
#endif
}

void WorkspaceFileScopeTest::ignoresBuiltInDirectories() {
    QVERIFY(isWorkspacePathIgnored(QStringLiteral(".git/config")));
    QVERIFY(isWorkspacePathIgnored(QStringLiteral("source/build/output.cpp")));
    QVERIFY(isWorkspacePathIgnored(QStringLiteral("node_modules/package/index.js")));
    QVERIFY(isWorkspacePathIgnored(QStringLiteral(".GIT/config")));
    QVERIFY(isWorkspacePathIgnored(QStringLiteral("BUILD/generated.cpp")));
    QVERIFY(isWorkspacePathIgnored(QStringLiteral("Node_Modules/package/index.js")));
    QVERIFY(isWorkspacePathIgnored(QStringLiteral("build\\cache\\entry")));
}

void WorkspaceFileScopeTest::retainsSimilarNamesAndNormalFiles() {
    QVERIFY(!isWorkspacePathIgnored(QStringLiteral("build-tools/main.cpp")));
    QVERIFY(!isWorkspacePathIgnored(QStringLiteral("my_node_modules/file.txt")));
    QVERIFY(!isWorkspacePathIgnored(QStringLiteral("src/.github/workflows/build.yml")));
    QVERIFY(!isWorkspacePathIgnored(QStringLiteral("src/main.cpp")));
}

void WorkspaceFileScopeTest::compilesSharedTextQuery() {
    const QRegularExpression literal =
        compileWorkspaceTextQuery({QStringLiteral("file.name"), false, true, false});
    QVERIFY(literal.match(QStringLiteral("FILE.NAME")).hasMatch());
    QVERIFY(!literal.match(QStringLiteral("myfile.name")).hasMatch());

    const QRegularExpression expression =
        compileWorkspaceTextQuery({QStringLiteral("item-(\\d+)"), true, false, true});
    const QRegularExpressionMatch match = expression.match(QStringLiteral("item-42"));
    QVERIFY(match.hasMatch());
    QCOMPARE(match.captured(1), QStringLiteral("42"));
    QCOMPARE(expandWorkspaceReplacement(
                 QStringLiteral("$2-$1"),
                 {QStringLiteral("foo42"), QStringLiteral("foo"), QStringLiteral("42")}, true,
                 false, true),
             QStringLiteral("42-foo"));
    QCOMPARE(expandWorkspaceReplacement(QStringLiteral("next"), {QStringLiteral("UPPER")}, false,
                                        true, false),
             QStringLiteral("NEXT"));
}

namespace {

void writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

void runReplace(WorkspaceReplaceRequest request, WorkspaceReplaceOutcome* outcome) {
    WorkspaceReplace replace;
    QSignalSpy completed(&replace, &WorkspaceReplace::completed);
    QVERIFY(replace.replaceAll(std::move(request)));
    QVERIFY2(completed.wait(5000), "Workspace replacement did not finish.");
    *outcome = qvariant_cast<WorkspaceReplaceOutcome>(completed.takeFirst().constFirst());
}

SearchOutcome runSearch(const QString& root, const QString& query, core::TextEncoding encoding,
                        const QString& ripgrepExecutable = {}, SearchBudgets budgets = {},
                        bool useRegularExpression = false) {
    WorkspaceSearch search(nullptr, {}, budgets, ripgrepExecutable);
    QSignalSpy completed(&search, &WorkspaceSearch::operationCompleted);
    search.searchText(root, query, 50, false, false, useRegularExpression, encoding);
    if (completed.isEmpty() && !completed.wait(5000))
        return {};
    return qvariant_cast<SearchOutcome>(completed.takeFirst().constFirst());
}

} // namespace

void WorkspaceFileScopeTest::replaceRejectsFilesOutsideWorkspace() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString workspace = temporary.filePath(QStringLiteral("workspace"));
    const QString outside = temporary.filePath(QStringLiteral("outside.txt"));
    QVERIFY(QDir().mkpath(workspace));
    writeFile(outside, QByteArrayLiteral("replace me"));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace;
    request.rootPath = workspace;
    request.query = QStringLiteral("replace");
    request.replacement = QStringLiteral("changed");
    request.targetPath = outside;
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);

    QCOMPARE(outcome.changedFiles, 0);
    QCOMPARE(outcome.skippedFiles, 1);
    QVERIFY(!outcome.safeDiagnostic.isEmpty());
    QFile unchanged(outside);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), QByteArrayLiteral("replace me"));
}

void WorkspaceFileScopeTest::rejectsCaseDistinctSiblingOnSensitiveVolume() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString workspacePath = temporary.filePath(QStringLiteral("Project"));
    const QString siblingPath = temporary.filePath(QStringLiteral("project"));
    QVERIFY(QDir().mkpath(workspacePath));
    QVERIFY(QDir().mkpath(siblingPath));
    const QByteArray workspaceIdentity = core::fileSystemEntryIdentity(workspacePath);
    const QByteArray siblingIdentity = core::fileSystemEntryIdentity(siblingPath);
    QVERIFY(!workspaceIdentity.isEmpty());
    QVERIFY(!siblingIdentity.isEmpty());
    if (workspaceIdentity == siblingIdentity)
        QSKIP("This volume is case-insensitive.");
    QCOMPARE(core::fileSystemCaseSensitivity(workspacePath), Qt::CaseSensitive);

    const QString outside = QDir(siblingPath).filePath(QStringLiteral("outside.txt"));
    writeFile(outside, QByteArrayLiteral("replace me"));

    WorkspaceService workspace;
    QVERIFY(workspace.openFolder(workspacePath));
    QVERIFY(!workspace.containsPath(siblingPath));
    QVERIFY(!workspace.containsPath(outside));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspacePath;
    request.rootPath = workspacePath;
    request.query = QStringLiteral("replace");
    request.replacement = QStringLiteral("changed");
    request.targetPath = outside;
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);

    QCOMPARE(outcome.changedFiles, 0);
    QCOMPARE(outcome.skippedFiles, 1);
    QFile unchanged(outside);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), QByteArrayLiteral("replace me"));
}

void WorkspaceFileScopeTest::replaceSkipsMalformedUtf8WithoutChangingBytes() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("legacy.txt"));
    const QByteArray original = QByteArray::fromHex("ff7265706c616365");
    writeFile(path, original);

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("replace");
    request.replacement = QStringLiteral("changed");
    request.targetPath = path;
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);

    QCOMPARE(outcome.changedFiles, 0);
    QCOMPARE(outcome.skippedFiles, 1);
    QVERIFY(!outcome.safeDiagnostic.isEmpty());
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
}

void WorkspaceFileScopeTest::replaceReportsPartialFailure() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString writable = workspace.filePath(QStringLiteral("writable.txt"));
    const QString missing = workspace.filePath(QStringLiteral("missing.txt"));
    writeFile(writable, QByteArrayLiteral("replace me"));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("replace");
    request.replacement = QStringLiteral("changed");
    request.targetPaths = {writable, missing};
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);

    QCOMPARE(outcome.changedFiles, 1);
    QCOMPARE(outcome.skippedFiles, 1);
    QVERIFY(!outcome.safeDiagnostic.isEmpty());
}

void WorkspaceFileScopeTest::searchesUtf16UsingConfiguredEncoding() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("utf16.txt"));
    writeFile(path, QByteArray::fromHex("680065006C006C006F000A00"));

    const SearchOutcome outcome =
        runSearch(workspace.path(), QStringLiteral("hello"), core::TextEncoding::Utf16Le);
    QCOMPARE(outcome.completion, core::CompletionKind::Succeeded);
    QCOMPARE(outcome.results.size(), 1);
    QCOMPARE(outcome.results.constFirst().line, 1);
    QCOMPARE(outcome.results.constFirst().matchedText, QStringLiteral("hello"));
}

void WorkspaceFileScopeTest::searchesConfiguredEncodingWithRipgrep() {
    const QString ripgrep = QStringLiteral(LITECODE_TEST_RIPGREP_PATH);
    QVERIFY2(QFileInfo(ripgrep).isExecutable(), qPrintable(ripgrep));

    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    QByteArray bytes;
    QString encodingError;
    QVERIFY2(core::encodeText(QStringLiteral("é hello\n").toUtf8(), core::TextEncoding::Utf16Le,
                              &bytes, &encodingError),
             qPrintable(encodingError));
    writeFile(workspace.filePath(QStringLiteral("utf16.txt")), bytes);

    const SearchOutcome outcome =
        runSearch(workspace.path(), QStringLiteral("hello"), core::TextEncoding::Utf16Le, ripgrep);
    QCOMPARE(outcome.completion, core::CompletionKind::Succeeded);
    QCOMPARE(outcome.results.size(), 1);
    QCOMPARE(outcome.results.constFirst().line, 1);
    QCOMPARE(outcome.results.constFirst().column, 3);
    QCOMPARE(outcome.results.constFirst().length, 5);
    QCOMPARE(outcome.results.constFirst().matchedText, QStringLiteral("hello"));
}

void WorkspaceFileScopeTest::searchEnginesEnforceContentReadLimit() {
    const QString ripgrep = QStringLiteral(LITECODE_TEST_RIPGREP_PATH);
    QVERIFY2(QFileInfo(ripgrep).isExecutable(), qPrintable(ripgrep));

    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString smallPath = workspace.filePath(QStringLiteral("small.txt"));
    writeFile(smallPath, QByteArrayLiteral("needle\n"));
    writeFile(workspace.filePath(QStringLiteral("oversized.txt")),
              QByteArray(64, 'x') + QByteArrayLiteral("\nneedle\n"));

    SearchBudgets budgets;
    budgets.maximumContentBytesPerFile = 32;
    budgets.contentBlockBytes = 7;

    const QString missingRipgrep = workspace.filePath(QStringLiteral("missing-ripgrep"));
    const SearchOutcome fallback = runSearch(workspace.path(), QStringLiteral("needle"),
                                             core::TextEncoding::Utf8, missingRipgrep, budgets);
    QCOMPARE(fallback.completion, core::CompletionKind::Succeeded);
    QCOMPARE(fallback.results.size(), 1);
    QCOMPARE(QFileInfo(fallback.results.constFirst().filePath).absoluteFilePath(),
             QFileInfo(smallPath).absoluteFilePath());
    QVERIFY(fallback.truncated);

    const SearchOutcome primary = runSearch(workspace.path(), QStringLiteral("needle"),
                                            core::TextEncoding::Utf8, ripgrep, budgets);
    QCOMPARE(primary.completion, core::CompletionKind::Succeeded);
    QCOMPARE(primary.results.size(), 1);
    QCOMPARE(QFileInfo(primary.results.constFirst().filePath).absoluteFilePath(),
             QFileInfo(smallPath).absoluteFilePath());
}

void WorkspaceFileScopeTest::workspaceTextSearchCapsAtFiveHundred() {
    QCOMPARE(defaultMaximumTextResults, 500);
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("matches.txt"));
    writeFile(path, QByteArrayLiteral("needle\n").repeated(500));

    WorkspaceSearch search(nullptr, {}, {}, workspace.filePath(QStringLiteral("missing-ripgrep")));
    QSignalSpy completed(&search, &WorkspaceSearch::operationCompleted);
    search.searchText(workspace.path(), QStringLiteral("needle"));
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
    const SearchOutcome outcome = qvariant_cast<SearchOutcome>(completed.takeFirst().constFirst());
    QCOMPARE(outcome.completion, core::CompletionKind::Succeeded);
    QCOMPARE(outcome.results.size(), 500);
    QCOMPARE(outcome.totalMatches, quint64{500});
    QCOMPARE(outcome.totalFiles, 1);
    QVERIFY(outcome.countsComplete);
    QVERIFY(!outcome.truncated);

    writeFile(path, QByteArrayLiteral("needle\n").repeated(501));
    writeFile(workspace.filePath(QStringLiteral("other.txt")), QByteArrayLiteral("needle\n"));
    search.searchText(workspace.path(), QStringLiteral("needle"));
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
    const SearchOutcome limited = qvariant_cast<SearchOutcome>(completed.takeFirst().constFirst());
    QCOMPARE(limited.results.size(), defaultMaximumTextResults);
    QCOMPARE(limited.totalMatches, quint64{502});
    QCOMPARE(limited.totalFiles, 2);
    QVERIFY(limited.countsComplete);
    QVERIFY(limited.truncated);

    const QString ripgrep = QStringLiteral(LITECODE_TEST_RIPGREP_PATH);
    QVERIFY2(QFileInfo(ripgrep).isExecutable(), qPrintable(ripgrep));
    WorkspaceSearch ripgrepSearch(nullptr, {}, {}, ripgrep);
    QSignalSpy ripgrepCompleted(&ripgrepSearch, &WorkspaceSearch::operationCompleted);
    ripgrepSearch.searchText(workspace.path(), QStringLiteral("needle"));
    QTRY_COMPARE_WITH_TIMEOUT(ripgrepCompleted.size(), 1, 5000);
    const SearchOutcome ripgrepOutcome =
        qvariant_cast<SearchOutcome>(ripgrepCompleted.takeFirst().constFirst());
    QCOMPARE(ripgrepOutcome.results.size(), defaultMaximumTextResults);
    QCOMPARE(ripgrepOutcome.totalMatches, quint64{502});
    QCOMPARE(ripgrepOutcome.totalFiles, 2);
    QVERIFY(ripgrepOutcome.countsComplete);
    QVERIFY(ripgrepOutcome.truncated);
}

void WorkspaceFileScopeTest::replacePreservesUtf16Encoding() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("utf16.txt"));
    writeFile(path, QByteArray::fromHex("FFFE680065006C006C006F00"));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("hello");
    request.replacement = QStringLiteral("world");
    request.encoding = core::TextEncoding::Utf8;
    request.targetPath = path;
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);

    QCOMPARE(outcome.changedFiles, 1);
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::ReadOnly));
    QCOMPARE(changed.readAll(), QByteArray::fromHex("FFFE77006F0072006C006400"));
}

void WorkspaceFileScopeTest::replaceAddsBomToBomlessUtf16() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("utf16.txt"));
    writeFile(path, QByteArray::fromHex("680065006C006C006F00"));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("hello");
    request.replacement = QStringLiteral("world");
    request.encoding = core::TextEncoding::Utf16Le;
    request.targetPath = path;
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);

    QCOMPARE(outcome.changedFiles, 1);
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::ReadOnly));
    QCOMPARE(changed.readAll(), QByteArray::fromHex("FFFE77006F0072006C006400"));
}

void WorkspaceFileScopeTest::replaceAppliesConfiguredUtf8Bom() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("utf8.txt"));
    writeFile(path, QByteArrayLiteral("hello"));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("hello");
    request.replacement = QStringLiteral("world");
    request.encoding = core::TextEncoding::Utf8Bom;
    request.targetPath = path;
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);

    QCOMPARE(outcome.changedFiles, 1);
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::ReadOnly));
    QCOMPARE(changed.readAll(), QByteArray::fromHex("EFBBBF776F726C64"));
}

void WorkspaceFileScopeTest::handlesWindows1252SearchAndReplace() {
    const QString ripgrep = QStringLiteral(LITECODE_TEST_RIPGREP_PATH);
    QVERIFY2(QFileInfo(ripgrep).isExecutable(), qPrintable(ripgrep));

    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("legacy.txt"));
    const QByteArray original = QByteArray::fromHex("636166E92068656C6C6F0A");
    writeFile(path, original);
    const QString unicodePath = workspace.filePath(QStringLiteral("unicode.txt"));
    writeFile(unicodePath, QByteArray::fromHex("FFFE680065006C006C006F000A00"));

    const SearchOutcome search = runSearch(workspace.path(), QStringLiteral("hello"),
                                           core::TextEncoding::Windows1252, ripgrep);
    QCOMPARE(search.completion, core::CompletionKind::Succeeded);
    QCOMPARE(search.results.size(), 2);
    const auto legacyResult = std::find_if(
        search.results.cbegin(), search.results.cend(), [&path](const SearchResult& result) {
            return QFileInfo(result.filePath).absoluteFilePath() ==
                   QFileInfo(path).absoluteFilePath();
        });
    QVERIFY(legacyResult != search.results.cend());
    QCOMPARE(legacyResult->column, 6);

    WorkspaceReplaceRequest lossy;
    lossy.workspaceRoot = workspace.path();
    lossy.rootPath = workspace.path();
    lossy.query = QStringLiteral("hello");
    lossy.replacement = QStringLiteral("😀");
    lossy.encoding = core::TextEncoding::Windows1252;
    lossy.targetPath = path;
    WorkspaceReplaceOutcome lossyOutcome;
    runReplace(std::move(lossy), &lossyOutcome);
    QCOMPARE(lossyOutcome.changedFiles, 0);
    QCOMPARE(lossyOutcome.skippedFiles, 1);
    QVERIFY(!lossyOutcome.safeDiagnostic.isEmpty());
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
    unchanged.close();

    WorkspaceReplaceRequest safe;
    safe.workspaceRoot = workspace.path();
    safe.rootPath = workspace.path();
    safe.query = QStringLiteral("hello");
    safe.replacement = QStringLiteral("world");
    safe.encoding = core::TextEncoding::Windows1252;
    safe.targetPath = path;
    WorkspaceReplaceOutcome safeOutcome;
    runReplace(std::move(safe), &safeOutcome);
    QCOMPARE(safeOutcome.changedFiles, 1);
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::ReadOnly));
    QCOMPARE(changed.readAll(), QByteArray::fromHex("636166E920776F726C640A"));
}

void WorkspaceFileScopeTest::handlesBuiltInCodePageSearchAndReplace() {
    const QString ripgrep = QStringLiteral(LITECODE_TEST_RIPGREP_PATH);
    QVERIFY2(QFileInfo(ripgrep).isExecutable(), qPrintable(ripgrep));

    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("dos.txt"));
    writeFile(path, QByteArray::fromHex("8268656C6C6F0A")); // CP437: éhello

    const SearchOutcome search =
        runSearch(workspace.path(), QStringLiteral("éhello"), core::TextEncoding::Cp437, ripgrep);
    QCOMPARE(search.completion, core::CompletionKind::Succeeded);
    QCOMPARE(search.results.size(), 1);

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("hello");
    request.replacement = QStringLiteral("world");
    request.encoding = core::TextEncoding::Cp437;
    request.targetPath = path;
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);
    QCOMPARE(outcome.changedFiles, 1);
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::ReadOnly));
    QCOMPARE(changed.readAll(), QByteArray::fromHex("82776F726C640A"));
}

void WorkspaceFileScopeTest::formatsPreviewWhitespaceAndLocatesEachMatch() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("matches.txt"));
    writeFile(path, QByteArrayLiteral("  foo foo  \n"));
    const SearchOutcome outcome =
        runSearch(workspace.path(), QStringLiteral("foo"), core::TextEncoding::Utf8,
                  workspace.filePath(QStringLiteral("missing-ripgrep")));
    QCOMPARE(outcome.completion, core::CompletionKind::Succeeded);
    QCOMPARE(outcome.results.size(), 2);
    QCOMPARE(outcome.results.at(0).preview, QStringLiteral("foo foo  "));
    QCOMPARE(outcome.results.at(0).previewMatchStart, 0);
    QCOMPARE(outcome.results.at(1).previewMatchStart, 4);
    const SearchOutcome spaced =
        runSearch(workspace.path(), QStringLiteral(" foo "), core::TextEncoding::Utf8,
                  workspace.filePath(QStringLiteral("missing-ripgrep")));
    QCOMPARE(spaced.results.size(), 1);
    QCOMPARE(spaced.results.constFirst().matchedText, QStringLiteral(" foo "));

    writeFile(path, QByteArray(80, ' ') + QByteArrayLiteral("foo   \n"));
    const SearchOutcome indented =
        runSearch(workspace.path(), QStringLiteral("foo"), core::TextEncoding::Utf8,
                  workspace.filePath(QStringLiteral("missing-ripgrep")));
    QCOMPARE(indented.results.size(), 1);
    QCOMPARE(indented.results.constFirst().previewMatchStart, 0);
    QCOMPARE(indented.results.constFirst().preview, QStringLiteral("foo   "));
}

void WorkspaceFileScopeTest::replacesOnlyRecordedMatches() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("matches.txt"));
    writeFile(path, QByteArrayLiteral("foo foo foo"));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("foo");
    request.replacement = QStringLiteral("bar");
    request.targetMatches = {{path, 1, 1, 3, QStringLiteral("foo")},
                             {path, 1, 9, 3, QStringLiteral("foo")}};
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);
    QCOMPARE(outcome.replacements, 2);
    QCOMPARE(outcome.changedFiles, 1);
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::ReadOnly));
    QCOMPARE(changed.readAll(), QByteArrayLiteral("bar foo bar"));
}

void WorkspaceFileScopeTest::replacesLineAnchoredMatches() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("anchored.txt"));
    writeFile(path, QByteArrayLiteral("foo\r\nfoo\nfoo\r\n"));

    const SearchOutcome search =
        runSearch(workspace.path(), QStringLiteral("^(foo)$"), core::TextEncoding::Utf8,
                  workspace.filePath(QStringLiteral("missing-ripgrep")), {}, true);
    QCOMPARE(search.completion, core::CompletionKind::Succeeded);
    QCOMPARE(search.results.size(), 3);

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("^(foo)$");
    request.replacement = QStringLiteral("$1!");
    request.useRegularExpression = true;
    for (const SearchResult& result : search.results) {
        if (result.line > 1)
            request.targetMatches.push_back(
                {result.filePath, result.line, result.column, result.length, result.matchedText});
    }
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);
    QCOMPARE(outcome.replacements, 2);
    QCOMPARE(outcome.changedFiles, 1);
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::ReadOnly));
    QCOMPARE(changed.readAll(), QByteArrayLiteral("foo\r\nfoo!\nfoo!\r\n"));
}

void WorkspaceFileScopeTest::rejectsStaleRecordedMatch() {
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    const QString path = workspace.filePath(QStringLiteral("changed.txt"));
    writeFile(path, QByteArrayLiteral("bar foo"));

    WorkspaceReplaceRequest request;
    request.workspaceRoot = workspace.path();
    request.rootPath = workspace.path();
    request.query = QStringLiteral("foo");
    request.replacement = QStringLiteral("baz");
    request.targetMatches = {{path, 1, 1, 3, QStringLiteral("foo")}};
    WorkspaceReplaceOutcome outcome;
    runReplace(std::move(request), &outcome);
    QCOMPARE(outcome.replacements, 0);
    QCOMPARE(outcome.changedFiles, 0);
    QCOMPARE(outcome.skippedFiles, 1);
    QFile unchanged(path);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), QByteArrayLiteral("bar foo"));
}

} // namespace litecode::workspace

QTEST_MAIN(litecode::workspace::WorkspaceFileScopeTest)

#include "tst_workspace_file_scope.moc"
