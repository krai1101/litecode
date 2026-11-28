#include "extensions/HostEnvironment.h"
#include "extensions/WorkbenchEnvironment.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace litecode::extensions;
class ExtensionEnvironmentTests final : public QObject {
    Q_OBJECT
  private slots:
    void globalLogGetterAndEventTrackOnlyEffectiveChanges() {
        EnvironmentInfo info{"LiteCode",
                             "/application",
                             "desktop",
                             "en",
                             "machine",
                             "session",
                             QDateTime::currentDateTimeUtc()};
        info.logLevel = LogLevel::Warning;
        HostEnvironment host(info, {});
        QVector<LogLevel> changes;
        auto listener = host.onDidChangeLogLevel()([&](LogLevel level) {
            QCOMPARE(host.logLevel(), level);
            changes.append(level);
        });
        QCOMPARE(host.logLevel(), LogLevel::Warning);
        host.acceptLogLevel(LogLevel::Warning);
        QVERIFY(changes.isEmpty());
        host.acceptLogLevel(LogLevel::Debug);
        QCOMPARE(changes, QVector<LogLevel>{LogLevel::Debug});
        host.acceptLogLevel(static_cast<LogLevel>(-1));
        QCOMPARE(host.logLevel(), LogLevel::Debug);
        host.shutdown();
        host.acceptLogLevel(LogLevel::Trace);
        QCOMPARE(host.logLevel(), LogLevel::Debug);
        QCOMPARE(changes.size(), 1);
        listener.dispose();
    }
    void shellChangesUpdateGetterBeforeEventAndStopOnShutdown() {
        EnvironmentInfo info{"LiteCode",
                             "/application",
                             "desktop",
                             "en",
                             "machine",
                             "session",
                             QDateTime::currentDateTimeUtc(),
                             false,
                             {},
                             UIKind::Desktop,
                             "/bin/first"};
        HostEnvironment host(info, {});
        QStringList changes;
        auto subscription = host.onDidChangeShell()([&](const QString& path) {
            QCOMPARE(host.shell(), path);
            changes.append(path);
        });
        QCOMPARE(host.shell(), QString("/bin/first"));
        host.acceptShell("/bin/first");
        QVERIFY(changes.isEmpty());
        host.acceptShell("/bin/second");
        QCOMPARE(changes, QStringList{"/bin/second"});
        host.acceptShell({});
        QCOMPARE(host.shell(), QString{});
        QCOMPARE(changes.size(), 2);
        subscription.dispose();
        host.acceptShell("/bin/third");
        QCOMPARE(changes.size(), 2);
        host.shutdown();
        host.acceptShell("/bin/after-shutdown");
        QCOMPARE(host.shell(), QString("/bin/third"));
        QVERIFY(!host.onDidChangeShell()([](const QString&) {}));
    }
    void identitySurvivesManagersAndHostsShareProcessSession() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("state/environment.json");
        WorkbenchEnvironment first(path);
        QVERIFY2(first.error().isEmpty(), qPrintable(first.error()));
        QVERIFY(!first.info().machineId.isEmpty());
        QVERIFY(!first.info().sessionId.isEmpty());
        QVERIFY(first.info().firstSessionDate.isValid());
        WorkbenchEnvironment second(path);
        QVERIFY(second.error().isEmpty());
        QCOMPARE(second.info().machineId, first.info().machineId);
        QCOMPARE(second.info().sessionId, first.info().sessionId);
        QCOMPARE(second.info().firstSessionDate, first.info().firstSessionDate);
        const auto decoded = wire::decodeEnvironmentInfo(wire::encodeEnvironmentInfo(first.info()));
        QVERIFY(decoded);
        HostEnvironment host(*decoded, {});
        HostEnvironment restarted(*decoded, {});
        QCOMPARE(host.appName(), first.info().appName);
        QCOMPARE(host.appRoot(), first.info().appRoot);
        QCOMPARE(host.appHost(), QString("desktop"));
        QCOMPARE(host.language(), QString("en"));
        QCOMPARE(host.machineId(), restarted.machineId());
        QCOMPARE(host.sessionId(), restarted.sessionId());
        QCOMPARE(host.uiKind(), UIKind::Desktop);
        QVERIFY(!host.isAppPortable());
        QVERIFY(!host.remoteName());
        QVERIFY(host.isNewAppInstall());
    }
    void newInstallAgeMatchesStrictDayBoundaryAndInvalidDates() {
        const auto first = QDateTime::fromString("2026-10-01T00:00:00.000Z", Qt::ISODateWithMs);
        QVERIFY(first.isValid());
        QVERIFY(isNewAppInstall(first, first));
        QVERIFY(isNewAppInstall(first, first.addMSecs(24LL * 60 * 60 * 1000 - 1)));
        QVERIFY(!isNewAppInstall(first, first.addDays(1)));
        QVERIFY(!isNewAppInstall(first, first.addDays(2)));
        QVERIFY(isNewAppInstall(first, first.addDays(-1)));
        QVERIFY(!isNewAppInstall({}, first));
        QVERIFY(!isNewAppInstall(first, {}));
    }
    void initializationRejectsMalformedTypesAndKeepsOptionalRemoteName() {
        const EnvironmentInfo info{"LiteCode",
                                   "/application",
                                   "desktop",
                                   "en",
                                   "machine",
                                   "session",
                                   QDateTime::currentDateTimeUtc(),
                                   false,
                                   QString("ssh-remote"),
                                   UIKind::Desktop};
        const auto encoded = wire::encodeEnvironmentInfo(info);
        const auto decoded = wire::decodeEnvironmentInfo(encoded);
        QVERIFY(decoded);
        QCOMPARE(decoded->remoteName, info.remoteName);
        auto invalid = encoded;
        invalid.insert("uiKind", 1.5);
        QVERIFY(!wire::decodeEnvironmentInfo(invalid));
        invalid = encoded;
        invalid.insert("isAppPortable", "false");
        QVERIFY(!wire::decodeEnvironmentInfo(invalid));
        invalid = encoded;
        invalid.insert("machineId", "");
        QVERIFY(!wire::decodeEnvironmentInfo(invalid));
        invalid = encoded;
        invalid.insert("firstSessionDate", "not a date");
        QVERIFY(!wire::decodeEnvironmentInfo(invalid));
        invalid = encoded;
        invalid.insert("remoteName", QJsonValue(QJsonValue::Null));
        QVERIFY(!wire::decodeEnvironmentInfo(invalid));
        invalid = encoded;
        invalid.remove("remoteName");
        QVERIFY(wire::decodeEnvironmentInfo(invalid));
        QVERIFY(!wire::decodeEnvironmentInfo(invalid)->remoteName);
    }
    void corruptIdentityIsReportedWithoutReplacingUserData() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("environment.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original("{corrupt but preserved");
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        WorkbenchEnvironment environment(path);
        QVERIFY(!environment.error().isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
};
QTEST_GUILESS_MAIN(ExtensionEnvironmentTests)
#include "tst_extension_environment.moc"
