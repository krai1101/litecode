#include "extensions/HostProgress.h"
#include "extensions/ProgressWire.h"
#include "extensions/WorkbenchProgress.h"
#include <QSignalSpy>
#include <QtTest>
#include <limits>
#include <stdexcept>

using namespace litecode::extensions;
namespace {
QJsonObject start(quint64 id, ProgressLocation location, bool cancellable = false) {
    ProgressOptions options;
    options.location = location;
    options.cancellable = cancellable;
    return {{"operation", "start"},
            {"id", QString::number(id)},
            {"options", wire::encodeProgressOptions(options)}};
}
QJsonObject end(quint64 id) { return {{"operation", "end"}, {"id", QString::number(id)}}; }
} // namespace
class ExtensionProgressTests final : public QObject {
    Q_OBJECT
  private slots:
    void mergedReportsFinishAndRetainedReporter() {
        WorkbenchProgress workbench;
        QVector<QJsonObject> sent;
        HostProgress host([&](const QJsonObject& value) {
            sent.append(value);
            QVERIFY(workbench.accept("fixture", "Fixture", value));
        });
        QSignalSpy changed(&workbench, &WorkbenchProgress::changed);
        QSignalSpy ended(&workbench, &WorkbenchProgress::ended);
        ProgressOptions options;
        options.location = ProgressLocation::Notification;
        auto session = host.begin(options);
        session.progress->report({QStringLiteral("first"), 10.0});
        session.progress->report({QStringLiteral("second"), 15.0});
        // Ending before the queued flush must still deliver accumulated deltas.
        session.finish();
        session.finish();
        QTRY_COMPARE(ended.size(), 1);
        QCOMPARE(sent.size(), 3);
        QCOMPARE(sent[0].value("operation").toString(), QStringLiteral("start"));
        QCOMPARE(sent[1].value("operation").toString(), QStringLiteral("report"));
        QCOMPARE(sent[2].value("operation").toString(), QStringLiteral("end"));
        QCOMPARE(changed.size(), 1);
        const auto task = changed.first().first().value<ProgressTaskInfo>();
        QCOMPARE(task.message.value(), QStringLiteral("second"));
        QCOMPARE(task.total.value(), 25.0);
        QVERIFY(!workbench.task("fixture", task.id));
        session.progress->report({QStringLiteral("late"), 20.0});
        QCoreApplication::processEvents();
        QCOMPARE(sent.size(), 3);
    }
    void cooperativeCancellationAndOwnerCleanup() {
        WorkbenchProgress workbench;
        quint64 id = 0;
        HostProgress host([&](const QJsonObject& value) {
            id = value.value("id").toString().toULongLong();
            QVERIFY(workbench.accept("fixture", "Fixture", value));
        });
        connect(&workbench, &WorkbenchProgress::cancelRequested, &host,
                [&](const QString&, quint64 taskId) { host.cancel(taskId); });
        ProgressOptions options;
        options.location = ProgressLocation::Notification;
        options.cancellable = true;
        auto session = host.begin(options);
        int cancelled = 0;
        auto subscription = session.token.onCancellationRequested([&] { ++cancelled; });
        QTRY_VERIFY(id != 0);
        QVERIFY(!workbench.cancel("other", id));
        QVERIFY(workbench.cancel("fixture", id));
        QVERIFY(!workbench.cancel("fixture", id));
        QCOMPARE(cancelled, 1);
        QVERIFY(session.token.isCancellationRequested());
        // A cancellation request does not complete the extension's task.
        QVERIFY(workbench.task("fixture", id));
        session.progress->report({QStringLiteral("Cleaning up"), {}});
        QTRY_COMPARE(workbench.task("fixture", id)->message.value_or(QString()),
                     QStringLiteral("Cleaning up"));
        workbench.removeOwner("fixture");
        QVERIFY(!workbench.task("fixture", id));
        session.finish();
        QCoreApplication::processEvents();
        QVERIFY(!workbench.task("fixture", id));
        subscription.dispose();
    }
    void locationAndProtocolValidation() {
        WorkbenchProgress workbench;
        QVERIFY(workbench.accept("fixture", "Fixture", start(1, ProgressLocation::Window, true)));
        QVERIFY(!workbench.cancel("fixture", 1));
        QVERIFY(workbench.accept("fixture", "Fixture",
                                 start(2, ProgressLocation::SourceControl, true)));
        QVERIFY(!workbench.cancel("fixture", 2));
        QVERIFY(!workbench.accept("fixture", "Fixture", start(2, ProgressLocation::Notification)));
        auto invalid = start(3, ProgressLocation::Notification);
        invalid.insert("id", "03");
        QVERIFY(!workbench.accept("fixture", "Fixture", invalid));
        QVERIFY(!workbench.accept("fixture", "Fixture",
                                  {{"operation", "report"}, {"id", "1"}, {"report", true}}));
        QVERIFY(workbench.accept("fixture", "Fixture", end(1)));
        QVERIFY(workbench.accept("fixture", "Fixture", end(1)));
        workbench.removeOwner("fixture");
        // A replacement host owns a new handle sequence.
        QVERIFY(workbench.accept("fixture", "Fixture", start(1, ProgressLocation::Notification)));
    }
    void reentrantCancellationDoesNotUpdateEndedTask() {
        WorkbenchProgress workbench;
        QVERIFY(
            workbench.accept("fixture", "Fixture", start(1, ProgressLocation::Notification, true)));
        QStringList events;
        connect(&workbench, &WorkbenchProgress::changed, &workbench,
                [&](const ProgressTaskInfo&) { events.append("changed"); });
        connect(&workbench, &WorkbenchProgress::ended, &workbench,
                [&](const QString&, quint64) { events.append("ended"); });
        connect(&workbench, &WorkbenchProgress::cancelRequested, &workbench,
                [&](const QString& owner, quint64 id) {
                    QVERIFY(workbench.accept(owner, "Fixture", end(id)));
                });
        QVERIFY(workbench.cancel("fixture", 1));
        QCOMPARE(events, QStringList({"changed", "ended"}));
        QVERIFY(!workbench.task("fixture", 1));
    }
    void shutdownAndInvalidReports() {
        int sent = 0;
        HostProgress host([&](const QJsonObject&) { ++sent; });
        ProgressOptions options;
        options.location = ProgressLocation::Notification;
        options.cancellable = true;
        auto session = host.begin(options);
        QVERIFY_EXCEPTION_THROWN(
            session.progress->report({{}, std::numeric_limits<double>::infinity()}),
            std::invalid_argument);
        host.shutdown();
        QVERIFY(session.token.isCancellationRequested());
        session.progress->report({QStringLiteral("late"), 1.0});
        session.finish();
        QCoreApplication::processEvents();
        QCOMPARE(sent, 0);
        QVERIFY_EXCEPTION_THROWN(host.begin(options), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(ExtensionProgressTests)
#include "tst_extension_progress.moc"
