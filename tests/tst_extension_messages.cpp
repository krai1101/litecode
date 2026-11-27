#include "extensions/HostWindow.h"
#include "extensions/WorkbenchMessages.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QtTest>

using namespace litecode::extensions;
class ExtensionMessageTests final : public QObject {
    Q_OBJECT
  private slots:
    void stringActionsAndDismissal() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchMessages workbench;
        HostWindow* window = nullptr;
        HostWindow host(directory.path(), [&](const QJsonObject& request) {
            QVERIFY(workbench.accept("fixture", "Fixture", request,
                [&, id = request.value("requestId").toString().toULongLong()](std::optional<int> selection, const QString& error) {
                    window->messages().complete(id, selection ? QJsonValue(*selection) : QJsonValue(QJsonValue::Null), error);
                }));
        });
        window = &host;
        QSignalSpy requests(&workbench, &WorkbenchMessages::requested);
        auto result = host.showInformationMessage("Info", QStringList{"Retry", "Open"});
        QTRY_COMPARE(requests.size(), 1);
        auto request = requests.first().first().value<WindowMessageRequest>();
        QCOMPARE(request.severity, 1);
        QVERIFY(!request.options.modal.value_or(false));
        QVERIFY(!workbench.select("other", request.id, 1));
        QVERIFY(!workbench.select("fixture", request.id, 2));
        QVERIFY(!result.isFinished());
        QVERIFY(workbench.select("fixture", request.id, 1));
        QTRY_VERIFY(result.isFinished());
        QCOMPARE(result.result().value(), QString("Open"));
        QVERIFY(!workbench.select("fixture", request.id, 0));
        requests.clear();
        auto dismissed = host.showWarningMessage("Warn");
        QTRY_COMPARE(requests.size(), 1);
        request = requests.first().first().value<WindowMessageRequest>();
        QCOMPARE(request.severity, 2);
        QVERIFY(workbench.select("fixture", request.id, {}));
        QTRY_VERIFY(dismissed.isFinished());
        QVERIFY(!dismissed.result());
    }
    void customItemPreservedAndHostExit() {
        struct CustomItem : MessageItem { int payload{}; };
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        WorkbenchMessages workbench;
        HostWindow* window = nullptr;
        HostWindow host(directory.path(), [&](const QJsonObject& request) {
            QVERIFY(workbench.accept("fixture", "Fixture", request,
                [&, id = request.value("requestId").toString().toULongLong()](std::optional<int> selection, const QString& error) {
                    window->messages().complete(id, selection ? QJsonValue(*selection) : QJsonValue(QJsonValue::Null), error);
                }));
        });
        window = &host;
        QSignalSpy requests(&workbench, &WorkbenchMessages::requested);
        QVector<CustomItem> actions{{{"Same", false}, 7}, {{"Same", true}, 42}};
        auto result = host.showErrorMessage("Error", MessageOptions{true, QString("Details")}, actions);
        QTRY_COMPARE(requests.size(), 1);
        auto request = requests.first().first().value<WindowMessageRequest>();
        QCOMPARE(request.severity, 3);
        QCOMPARE(request.options.detail.value(), QString("Details"));
        QVERIFY(request.options.modal.value());
        QVERIFY(request.items.last().isCloseAffordance.value());
        actions.last().payload = 999;
        QVERIFY(workbench.select("fixture", request.id, 1));
        QTRY_VERIFY(result.isFinished());
        QCOMPARE(result.result()->payload, 42);
        requests.clear();
        auto pending = host.showInformationMessage("Pending");
        QTRY_COMPARE(requests.size(), 1);
        workbench.removeOwner("fixture");
        QTRY_VERIFY(pending.isFinished());
        QVERIFY_EXCEPTION_THROWN(pending.result(), std::runtime_error);
        auto shutdown = host.showInformationMessage("Not sent yet");
        host.shutdown();
        QTRY_VERIFY(shutdown.isFinished());
        QVERIFY_EXCEPTION_THROWN(shutdown.result(), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(ExtensionMessageTests)
#include "tst_extension_messages.moc"
