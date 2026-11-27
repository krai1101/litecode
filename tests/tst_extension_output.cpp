#include "extensions/HostOutput.h"
#include "extensions/WorkbenchOutput.h"
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace litecode::extensions;
class ExtensionOutputTests final : public QObject {
    Q_OBJECT
  private slots:
    void globalLevelsFlowToInheritedChannelsAndRespectResourceOverrides() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchOutput workbench;
        HostOutput host(directory.path(), [&](const QJsonObject& message) { QVERIFY(workbench.accept("owner", message)); });
        connect(&workbench, &WorkbenchOutput::globalLogLevelChanged, &host, [&](int level) { host.setGlobalLogLevel(static_cast<LogLevel>(level)); });
        connect(&workbench, &WorkbenchOutput::logLevelRequested, &host,
            [&](const QString&, const QString& id, int level) { host.setLogLevel(id, static_cast<LogLevel>(level)); });
        workbench.setGlobalLogLevel(LogLevel::Trace);
        auto inherited = host.createOutputChannel("Inherited", LogOutputChannelOptions{});
        auto explicitLevel = host.createOutputChannel("Explicit", LogOutputChannelOptions{});
        QCOMPARE(inherited->logLevel(), LogLevel::Trace);
        QCOMPARE(explicitLevel->logLevel(), LogLevel::Trace);
        QTRY_COMPARE(workbench.channels().size(), 2);
        QString inheritedId, explicitId;
        for (const auto& channel : workbench.channels()) {
            if (channel.name == "Inherited") inheritedId = channel.id;
            else if (channel.name == "Explicit") explicitId = channel.id;
        }
        QVERIFY(!inheritedId.isEmpty());
        QVERIFY(!explicitId.isEmpty());
        workbench.setLogLevel(explicitId, LogLevel::Error);
        QVector<LogLevel> changes;
        auto listener = inherited->onDidChangeLogLevel()([&](LogLevel level) { changes.append(level); });
        workbench.setGlobalLogLevel(LogLevel::Debug);
        QCOMPARE(inherited->logLevel(), LogLevel::Debug);
        QCOMPARE(explicitLevel->logLevel(), LogLevel::Error);
        QCOMPARE(workbench.channel(inheritedId)->level, LogLevel::Debug);
        QCOMPARE(changes, QVector<LogLevel>{LogLevel::Debug});
        workbench.setGlobalLogLevel(LogLevel::Error);
        // Even though the effective level is already Error, explicitly choosing
        // the global level clears the old resource override on both sides.
        workbench.setLogLevel(explicitId, LogLevel::Error);
        workbench.setGlobalLogLevel(LogLevel::Warning);
        QCOMPARE(explicitLevel->logLevel(), LogLevel::Warning);
        QCOMPARE(inherited->logLevel(), LogLevel::Warning);
        const auto eventCount = changes.size();
        workbench.setGlobalLogLevel(LogLevel::Warning);
        QCOMPARE(changes.size(), eventCount);
        inherited->info("filtered-global-info");
        inherited->warn("visible-global-warning");
        QTRY_VERIFY(workbench.channel(inheritedId)->text.contains("visible-global-warning"));
        QVERIFY(!workbench.channel(inheritedId)->text.contains("filtered-global-info"));
        auto later = host.createOutputChannel("Created later", LogOutputChannelOptions{});
        QCOMPARE(later->logLevel(), LogLevel::Warning);
        host.shutdown();
        host.setGlobalLogLevel(LogLevel::Trace);
        QCOMPARE(inherited->logLevel(), LogLevel::Warning);
        listener.dispose();
    }
    void invalidNamesAndWriteFailure() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchOutput workbench;
        bool valid = true;
        HostOutput host(directory.path(), [&](const QJsonObject& message) {
            valid = workbench.accept("fixture", message) && valid;
        });
        QVERIFY_EXCEPTION_THROWN(host.createOutputChannel(" \t"), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(host.createOutputChannel("Name", QString(" \t")), std::invalid_argument);
        auto channel = host.createOutputChannel("Failure");
        QTRY_COMPARE(workbench.channels().size(), 1);
        // A directory at the output file's path reliably fails writes on all
        // three platforms, without depending on runner permissions or chmod.
        const auto id = workbench.channels().first().id;
        QVERIFY(QDir(directory.path()).mkdir(id + ".log"));
        QSignalSpy errors(&workbench, &WorkbenchOutput::channelError);
        channel->append("must not pretend this persisted");
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!errors.first().first().toString().isEmpty());
        QVERIFY(workbench.channel(id)->text.isEmpty());
        QVERIFY(valid);
    }
    void orderedOperationsAndPersistentLog() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchOutput workbench;
        bool valid = true;
        HostOutput host(directory.path(), [&](const QJsonObject& message) {
            valid = workbench.accept("fixture", message) && valid;
        });
        QSignalSpy reveal(&workbench, &WorkbenchOutput::revealRequested);
        QSignalSpy hide(&workbench, &WorkbenchOutput::hideRequested);
        auto channel = host.createOutputChannel("  Fixture  ");
        QCOMPARE(channel->name(), QString("Fixture"));
        channel->append("before");
        channel->appendLine("");
        channel->clear();
        channel->append("discarded");
        channel->replace("replacement");
        channel->appendLine(" tail");
        channel->show(ViewColumn::Beside, true);
        channel->hide();
        QTRY_COMPARE(hide.size(), 1);
        QVERIFY(valid);
        QCOMPARE(workbench.channels().size(), 1);
        QCOMPARE(workbench.channels().first().text, QString("replacement tail\n"));
        QCOMPARE(reveal.size(), 1);
        QVERIFY(reveal.first().at(1).toBool());
        const auto files = QDir(directory.path()).entryList({"*.log"}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(directory.filePath(files.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("before\ndiscardedreplacement tail\n"));
        channel->dispose();
        channel->dispose();
        QTRY_VERIFY(workbench.channels().isEmpty());
        QVERIFY_EXCEPTION_THROWN(channel->append("closed"), std::runtime_error);
    }
    void unicodeTransportBoundaries() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchOutput workbench;
        bool valid = true;
        QVector<QString> chunks;
        HostOutput host(directory.path(), [&](const QJsonObject& message) {
            if (message.value("operation") == "append") chunks.append(message.value("text").toString());
            valid = workbench.accept("fixture", message) && valid;
        });
        auto channel = host.createOutputChannel("Unicode");
        const auto value = QString(32767, QLatin1Char('x')) + QString::fromUtf8("\xf0\x9f\x98\x80") + QString(32768, QLatin1Char('y'));
        channel->append(value);
        QTRY_COMPARE(chunks.size(), 3);
        QVERIFY(valid);
        QCOMPARE(workbench.channels().first().text, value);
        for (const auto& chunk : chunks) {
            QVERIFY(chunk.size() <= 32768);
            QVERIFY(!chunk.back().isHighSurrogate());
            QVERIFY(!chunk.front().isLowSurrogate());
        }
    }
    void logRegistryAndLevelEvents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchOutput workbench;
        bool valid = true;
        HostOutput host(directory.path(), [&](const QJsonObject& message) {
            valid = workbench.accept("fixture", message) && valid;
        });
        connect(&workbench, &WorkbenchOutput::logLevelRequested, &host,
            [&](const QString&, const QString& id, int level) { host.setLogLevel(id, static_cast<LogLevel>(level)); });
        connect(&workbench, &WorkbenchOutput::clearRequested, &host,
            [&](const QString&, const QString& id) { host.clear(id); });
        auto channel = host.createOutputChannel("Logger", LogOutputChannelOptions{});
        const auto original = channel.get();
        channel.reset();
        channel = host.createOutputChannel("Logger", LogOutputChannelOptions{});
        QCOMPARE(channel.get(), original);
        QTRY_COMPARE(workbench.channels().size(), 1);
        const auto id = workbench.channels().first().id;
        QVector<LogLevel> changes;
        auto listener = channel->onDidChangeLogLevel()([&](LogLevel level) { changes.append(level); });
        channel->trace("filtered");
        channel->info("visible", {42});
        QTRY_VERIFY(workbench.channel(id)->text.contains("[info] visible 42\n"));
        QVERIFY(!workbench.channel(id)->text.contains("filtered"));
        workbench.setLogLevel(id, LogLevel::Error);
        QCOMPARE(channel->logLevel(), LogLevel::Error);
        QCOMPARE(changes, QVector<LogLevel>{LogLevel::Error});
        workbench.setLogLevel(id, LogLevel::Error);
        QCOMPARE(changes.size(), 1);
        workbench.clear(id);
        QTRY_VERIFY(workbench.channel(id)->text.isEmpty());
        channel->warn("hidden warning");
        channel->error("failure");
        QTRY_VERIFY(workbench.channel(id)->text.contains("[error] failure\n"));
        QVERIFY(!workbench.channel(id)->text.contains("hidden warning"));
        workbench.setLogLevel(id, LogLevel::Off);
        workbench.clear(id);
        channel->error("hidden error");
        QTRY_VERIFY(workbench.channel(id)->text.isEmpty());
        channel->dispose();
        QTRY_VERIFY(workbench.channels().isEmpty());
        auto replacement = host.createOutputChannel("Logger", LogOutputChannelOptions{});
        QVERIFY(replacement.get() != channel.get());
        QTRY_COMPARE(workbench.channels().size(), 1);
        QVERIFY(valid);
        listener.dispose();
    }
    void ownershipAndShutdown() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchOutput workbench;
        HostOutput host(directory.path(), [&](const QJsonObject& message) { QVERIFY(workbench.accept("owner", message)); });
        auto channel = host.createOutputChannel("Owned");
        QTRY_COMPARE(workbench.channels().size(), 1);
        const auto id = workbench.channels().first().id;
        for (const auto& operation : {"append", "clear", "show", "hide", "dispose"})
            QVERIFY(!workbench.accept("other", {{"id", id}, {"operation", operation}, {"text", "forged"}, {"preserveFocus", true}}));
        QVERIFY(!workbench.accept("owner", {{"id", id}, {"operation", "append"}, {"text", QString(32769, QLatin1Char('x'))}}));
        host.shutdown();
        host.shutdown();
        QVERIFY_EXCEPTION_THROWN(channel->clear(), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(host.createOutputChannel("After shutdown"), std::runtime_error);
        channel->dispose();
        workbench.removeOwner("other");
        QCOMPARE(workbench.channels().size(), 1);
        workbench.removeOwner("owner");
        QVERIFY(workbench.channels().isEmpty());
    }
};
QTEST_GUILESS_MAIN(ExtensionOutputTests)
#include "tst_extension_output.moc"
