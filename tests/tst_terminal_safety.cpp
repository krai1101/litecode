#include "terminal/TerminalInputQueue.h"
#include "terminal/TerminalSessionService.h"
#include "terminal/VTermEngine.h"
#ifdef Q_OS_WIN
#include "terminal/WindowsConPtyBackend.h"
#endif
#ifndef Q_OS_WIN
#include "terminal/UnixPtyBackend.h"
#endif

#include <QHash>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using litecode::terminal::terminalTitleCharacterLimit;
using litecode::terminal::VTermEngine;

class TerminalSafetyTest final : public QObject {
    Q_OBJECT

  private slots:
    void terminalTitleIsBounded();
    void inputQueueIsBoundedAndPreservesPartialWrites();
#ifdef Q_OS_WIN
    void windowsFallbackTerminatesChildProcesses();
    void windowsBurstOutputIsNotTruncated();
    void multipleConPtySessionsRemainIndependentWhenOneIsRemoved();
#endif
#ifndef Q_OS_WIN
    void unixBackendPassesConfiguredEnvironment();
    void unixBurstOutputIsNotTruncated();
    void unixBackendStartsAtRequestedSize();
#endif
};

void TerminalSafetyTest::terminalTitleIsBounded() {
    VTermEngine engine;
    const QByteArray oversizedTitle(terminalTitleCharacterLimit + 1'024, 'a');

    engine.feed(QByteArrayLiteral("\x1b]2;") + oversizedTitle + QByteArrayLiteral("\x07"));

    QCOMPARE(engine.title().size(), terminalTitleCharacterLimit);
    QCOMPARE(engine.title(), QString(terminalTitleCharacterLimit, QLatin1Char('a')));
}

void TerminalSafetyTest::inputQueueIsBoundedAndPreservesPartialWrites() {
    litecode::terminal::TerminalInputQueue queue(8);
    QVERIFY(queue.enqueue(QByteArrayLiteral("abcde")));
    QCOMPARE(queue.nextSlice(3), QByteArrayLiteral("abc"));
    queue.consume(3);
    QCOMPARE(queue.nextSlice(8), QByteArrayLiteral("de"));
    QVERIFY(queue.enqueue(QByteArrayLiteral("123456")));
    QVERIFY(!queue.enqueue(QByteArrayLiteral("x")));
    queue.consume(2);
    QCOMPARE(queue.nextSlice(3), QByteArrayLiteral("123"));
    queue.consume(6);
    QVERIFY(queue.isEmpty());
    QCOMPARE(queue.queuedBytes(), 0);
}

#ifdef Q_OS_WIN
void TerminalSafetyTest::multipleConPtySessionsRemainIndependentWhenOneIsRemoved() {
    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());

    const auto audit = std::make_shared<litecode::terminal::WindowsConPtyResourceAudit>();
    litecode::terminal::TerminalSessionService sessions(nullptr, [audit] {
        return std::make_unique<litecode::terminal::WindowsConPtyBackend>(
            nullptr, litecode::terminal::WindowsConPtyFailurePoint::None, audit);
    });
    const auto commandPrompt = std::find_if(
        sessions.profiles().cbegin(), sessions.profiles().cend(),
        [](const auto& profile) { return profile.id == QStringLiteral("command-prompt"); });
    QVERIFY(commandPrompt != sessions.profiles().cend());

    QVector<litecode::core::OperationId> started;
    QVector<litecode::core::OperationId> removed;
    QHash<quint64, QByteArray> output;
    QStringList errors;
    connect(&sessions, &litecode::terminal::TerminalSessionService::sessionStarted, this,
            [&started](litecode::core::OperationId id) { started.append(id); });
    connect(&sessions, &litecode::terminal::TerminalSessionService::outputReady, this,
            [&output](litecode::core::OperationId id, const QByteArray& bytes) {
                output[id.value()].append(bytes);
            });
    connect(&sessions, &litecode::terminal::TerminalSessionService::sessionRemoved, this,
            [&removed](litecode::core::OperationId id) { removed.append(id); });
    connect(&sessions, &litecode::terminal::TerminalSessionService::sessionError, this,
            [&errors](litecode::core::OperationId, const QString& error) { errors.append(error); });

    const auto first = sessions.createSession(commandPrompt->id, workingDirectory.path());
    const auto second = sessions.createSession(commandPrompt->id, workingDirectory.path());
    const auto third = sessions.createSession(commandPrompt->id, workingDirectory.path());
    QVERIFY(first != second && second != third && first != third);
    QVERIFY(sessions.sendInput(first, QByteArrayLiteral("echo PRELAUNCH_INPUT_READY\r")));
    QTRY_COMPARE_WITH_TIMEOUT(started.size(), 3, 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(output.value(first.value()).contains("PRELAUNCH_INPUT_READY"), 10'000);

    // The expected strings are assembled by cmd.exe; input echo alone cannot satisfy these.
    QVERIFY(sessions.sendInput(first, QByteArrayLiteral("set LITECODE_PROBE=FIRST_READY\r"
                                                        "echo LITECODE_%LITECODE_PROBE%\r")));
    QVERIFY(sessions.sendInput(second, QByteArrayLiteral("set LITECODE_PROBE=SECOND_READY\r"
                                                         "echo LITECODE_%LITECODE_PROBE%\r")));
    QVERIFY(sessions.sendInput(third, QByteArrayLiteral("set LITECODE_PROBE=THIRD_READY\r"
                                                        "echo LITECODE_%LITECODE_PROBE%\r")));
    QTRY_VERIFY_WITH_TIMEOUT(output.value(first.value()).contains("LITECODE_FIRST_READY"), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(output.value(second.value()).contains("LITECODE_SECOND_READY"),
                             10'000);
    QTRY_VERIFY_WITH_TIMEOUT(output.value(third.value()).contains("LITECODE_THIRD_READY"), 10'000);

    sessions.requestRemove(second);
    QTRY_VERIFY_WITH_TIMEOUT(removed.contains(second), 10'000);
    QCOMPARE(sessions.sessions().size(), 2);
    QVERIFY(sessions.sendInput(first, QByteArrayLiteral("set LITECODE_PROBE=FIRST_SURVIVED\r"
                                                        "echo LITECODE_%LITECODE_PROBE%\r")));
    QVERIFY(sessions.sendInput(third, QByteArrayLiteral("set LITECODE_PROBE=THIRD_SURVIVED\r"
                                                        "echo LITECODE_%LITECODE_PROBE%\r")));
    QTRY_VERIFY_WITH_TIMEOUT(output.value(first.value()).contains("LITECODE_FIRST_SURVIVED"),
                             10'000);
    QTRY_VERIFY_WITH_TIMEOUT(output.value(third.value()).contains("LITECODE_THIRD_SURVIVED"),
                             10'000);

    sessions.requestRemove(first);
    sessions.requestRemove(third);
    QTRY_VERIFY_WITH_TIMEOUT(sessions.sessions().isEmpty(), 10'000);
    QTRY_VERIFY_WITH_TIMEOUT(audit->allReleased(), 5'000);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
}

void TerminalSafetyTest::windowsFallbackTerminatesChildProcesses() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString marker = temporary.filePath(QStringLiteral("child-survived.txt"));

    litecode::terminal::TerminalProfile profile;
    profile.id = QStringLiteral("process-tree-test");
    profile.displayName = QStringLiteral("Process tree test");
    profile.executable = QStringLiteral(LITECODE_TERMINAL_PROCESS_HELPER);
    profile.arguments = {QStringLiteral("--parent"), marker};
    profile.environment = QProcessEnvironment::systemEnvironment();

    const auto audit = std::make_shared<litecode::terminal::WindowsConPtyResourceAudit>();
    litecode::terminal::WindowsConPtyBackend backend(
        nullptr, litecode::terminal::WindowsConPtyFailurePoint::JobAssignment, audit);
    QByteArray output;
    connect(&backend, &litecode::terminal::ITerminalBackend::outputReady, this,
            [&output](const QByteArray& bytes) { output += bytes; });
    QSignalSpy completed(&backend, &litecode::terminal::ITerminalBackend::completed);

    backend.start(litecode::core::OperationId(1), profile, temporary.path());
    QTRY_VERIFY_WITH_TIMEOUT(output.contains("child-ready"), 5'000);
    backend.requestStop();
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5'000);
    QTest::qWait(2'500);
    QVERIFY(!QFileInfo::exists(marker));
    QTRY_VERIFY_WITH_TIMEOUT(audit->allReleased(), 2'000);
}

void TerminalSafetyTest::windowsBurstOutputIsNotTruncated() {
    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());

    litecode::terminal::TerminalProfile profile;
    profile.id = QStringLiteral("burst-test");
    profile.displayName = QStringLiteral("Burst test");
    profile.executable = QStringLiteral(LITECODE_TERMINAL_PROCESS_HELPER);
    profile.arguments = {QStringLiteral("--burst"), QStringLiteral("unused")};
    profile.environment = QProcessEnvironment::systemEnvironment();

    litecode::terminal::WindowsConPtyBackend backend;
    QByteArray output;
    connect(&backend, &litecode::terminal::ITerminalBackend::outputReady, this,
            [&output](const QByteArray& bytes) { output += bytes; });
    connect(&backend, &litecode::terminal::ITerminalBackend::started, this,
            [] { QTest::qSleep(250); });
    QSignalSpy completed(&backend, &litecode::terminal::ITerminalBackend::completed);

    backend.start(litecode::core::OperationId(1), profile, workingDirectory.path());
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15'000);
    const qsizetype begin = output.indexOf("BURST_BEGIN");
    const qsizetype end = output.indexOf("BURST_COMPLETE");
    QVERIFY(begin >= 0);
    QVERIFY(end > begin);
    qsizetype cursor = begin + sizeof("BURST_BEGIN") - 1;
    for (int index = 0; index < 40'000; ++index) {
        const QByteArray number = QByteArray::number(index).rightJustified(5, '0');
        const QByteArray record = QByteArrayLiteral("ROW") + number + ':' +
                                  QByteArray(32, static_cast<char>('A' + index % 26));
        cursor = output.indexOf(record, cursor);
        QVERIFY2(cursor >= 0 && cursor < end,
                 qPrintable(QStringLiteral("Missing burst record %1").arg(index)));
        cursor += record.size();
    }
    QVERIFY(!output.contains("[terminal output truncated]"));
}
#endif

#ifndef Q_OS_WIN
void TerminalSafetyTest::unixBackendPassesConfiguredEnvironment() {
    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());

    litecode::terminal::TerminalProfile profile;
    profile.id = QStringLiteral("environment-test");
    profile.displayName = QStringLiteral("Environment test");
    profile.executable = QStringLiteral("/usr/bin/env");
    profile.environment.insert(QStringLiteral("LITECODE_TERMINAL_ENV_TEST"),
                               QStringLiteral("configured"));

    litecode::terminal::UnixPtyBackend backend;
    QByteArray output;
    connect(&backend, &litecode::terminal::ITerminalBackend::outputReady, this,
            [&output](const QByteArray& bytes) { output += bytes; });
    QSignalSpy completed(&backend, &litecode::terminal::ITerminalBackend::completed);

    backend.start(litecode::core::OperationId(1), profile, workingDirectory.path());

    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5'000);
    QVERIFY(output.contains("LITECODE_TERMINAL_ENV_TEST=configured"));
}

void TerminalSafetyTest::unixBurstOutputIsNotTruncated() {
    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());

    litecode::terminal::TerminalProfile profile;
    profile.id = QStringLiteral("burst-test");
    profile.displayName = QStringLiteral("Burst test");
    profile.executable = QStringLiteral("/usr/bin/seq");
    profile.arguments = {QStringLiteral("1"), QStringLiteral("300000")};
    profile.environment = QProcessEnvironment::systemEnvironment();

    litecode::terminal::UnixPtyBackend backend;
    QByteArray output;
    connect(&backend, &litecode::terminal::ITerminalBackend::outputReady, this,
            [&output](const QByteArray& bytes) { output += bytes; });
    QSignalSpy completed(&backend, &litecode::terminal::ITerminalBackend::completed);

    backend.start(litecode::core::OperationId(1), profile, workingDirectory.path());
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 15'000);
    QCOMPARE(output.count('\n'), 300000);
    QVERIFY(output.endsWith("300000\r\n"));
}

void TerminalSafetyTest::unixBackendStartsAtRequestedSize() {
    QTemporaryDir workingDirectory;
    QVERIFY(workingDirectory.isValid());

    litecode::terminal::TerminalProfile profile;
    profile.id = QStringLiteral("size-test");
    profile.displayName = QStringLiteral("Size test");
    profile.executable = QStandardPaths::findExecutable(QStringLiteral("stty"));
    QVERIFY2(!profile.executable.isEmpty(), "stty was not found in PATH");
    profile.arguments = {QStringLiteral("size")};
    profile.environment = QProcessEnvironment::systemEnvironment();

    litecode::terminal::UnixPtyBackend backend;
    backend.resizeTerminal(QSize(96, 31));
    QByteArray output;
    connect(&backend, &litecode::terminal::ITerminalBackend::outputReady, this,
            [&output](const QByteArray& bytes) { output += bytes; });
    QSignalSpy completed(&backend, &litecode::terminal::ITerminalBackend::completed);

    backend.start(litecode::core::OperationId(1), profile, workingDirectory.path());
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5'000);
    const auto result = qvariant_cast<litecode::terminal::TerminalExit>(completed.first().first());
    QVERIFY2(!result.crashed, output.constData());
    QCOMPARE(result.exitCode, 0);
    QCOMPARE(output.trimmed(), QByteArrayLiteral("31 96"));
}
#endif

QTEST_GUILESS_MAIN(TerminalSafetyTest)

#include "tst_terminal_safety.moc"
