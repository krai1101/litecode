#include "extensions/HostDialogs.h"
#include "extensions/WorkbenchDialogs.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>
#include <stdexcept>

using namespace litecode::extensions;
class ExtensionDialogTests final : public QObject {
    Q_OBJECT
  private slots:
    void selectionsAndCancellation() {
        WorkbenchDialogs workbench;
        HostDialogs* service = nullptr;
        HostDialogs host([&](const QJsonObject& request) {
            QVERIFY(
                workbench.accept("fixture", request,
                                 [&, id = request.value("requestId").toString().toULongLong()](
                                     std::optional<QVector<QUrl>> values, const QString& error) {
                                     QJsonValue result(QJsonValue::Null);
                                     if (values) {
                                         QJsonArray uris;
                                         for (const auto& uri : *values)
                                             uris.append(uri.toString(QUrl::FullyEncoded));
                                         result = uris;
                                     }
                                     service->complete(id, result, error);
                                 }));
        });
        service = &host;
        QSignalSpy requests(&workbench, &WorkbenchDialogs::requested);
        OpenDialogOptions options;
        options.canSelectMany = true;
        auto opened = host.showOpenDialog(options);
        QTRY_COMPARE(requests.size(), 1);
        auto request = requests.takeFirst().first().value<FileDialogRequest>();
        QVERIFY(std::get<OpenDialogOptions>(request.options).canSelectMany.value());
        const QVector<QUrl> values{QUrl::fromLocalFile("/directory/a.py"),
                                   QUrl::fromLocalFile("/directory/b.py")};
        QVERIFY(!workbench.complete("other", request.id, values));
        QVERIFY(workbench.complete("fixture", request.id, values));
        QTRY_VERIFY(opened.isFinished());
        QCOMPARE(opened.result().value(), values);
        auto saved = host.showSaveDialog();
        QTRY_COMPARE(requests.size(), 1);
        request = requests.takeFirst().first().value<FileDialogRequest>();
        QVERIFY(std::holds_alternative<SaveDialogOptions>(request.options));
        QVERIFY(!workbench.complete("fixture", request.id, values));
        QVERIFY(workbench.complete("fixture", request.id, QVector<QUrl>{values.first()}));
        QTRY_VERIFY(saved.isFinished());
        QCOMPARE(saved.result().value(), values.first());
        auto cancelled = host.showOpenDialog();
        QTRY_COMPARE(requests.size(), 1);
        request = requests.takeFirst().first().value<FileDialogRequest>();
        QVERIFY(workbench.complete("fixture", request.id, {}));
        QTRY_VERIFY(cancelled.isFinished());
        QVERIFY(!cancelled.result());
        auto removed = host.showSaveDialog();
        QTRY_COMPARE(requests.size(), 1);
        workbench.removeOwner("fixture");
        QTRY_VERIFY(removed.isFinished());
        QVERIFY_EXCEPTION_THROWN(removed.result(), std::runtime_error);
    }
    void invalidResultAndShutdown() {
        quint64 id = 0;
        HostDialogs host([&](const QJsonObject& request) {
            id = request.value("requestId").toString().toULongLong();
        });
        auto empty = host.showOpenDialog();
        QTRY_VERIFY(id != 0);
        host.complete(id, QJsonArray{}, {});
        QTRY_VERIFY(empty.isFinished());
        QVERIFY_EXCEPTION_THROWN(empty.result(), std::runtime_error);
        id = 0;
        auto invalid = host.showSaveDialog();
        QTRY_VERIFY(id != 0);
        host.complete(id, QJsonArray{true}, {});
        QTRY_VERIFY(invalid.isFinished());
        QVERIFY_EXCEPTION_THROWN(invalid.result(), std::runtime_error);
        auto shutdown = host.showOpenDialog();
        host.shutdown();
        QTRY_VERIFY(shutdown.isFinished());
        QVERIFY_EXCEPTION_THROWN(shutdown.result(), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(ExtensionDialogTests)
#include "tst_extension_dialogs.moc"
