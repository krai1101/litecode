#include "extensions/HostInputBox.h"
#include "extensions/InputBoxWire.h"
#include "extensions/WorkbenchInputBox.h"
#include "ui/extensions/InputBoxService.h"
#include "ui/components/QuickInputDialog.h"
#include <QLineEdit>
#include <QPromise>
#include <QSignalSpy>
#include <QtTest>
using namespace litecode::extensions;
class InputBoxTests final : public QObject {
    Q_OBJECT
  private slots:
    void optionsAndValidationRoundTrip() {
        InputBoxOptions options;
        options.title = "Title"; options.value = "  secret  "; options.prompt = "Prompt";
        options.placeHolder = "Placeholder"; options.password = true; options.ignoreFocusOut = false;
        options.valueSelection = QPair{2, 5};
        const auto encoded = wire::encodeInputBoxOptions(options);
        const auto decoded = wire::decodeInputBoxOptions(encoded);
        QVERIFY(decoded);
        QCOMPARE(wire::encodeInputBoxOptions(*decoded), encoded);
        auto invalid = encoded; invalid.insert("password", "true");
        QVERIFY(!wire::decodeInputBoxOptions(invalid));
        QVERIFY(!wire::decodeInputBoxValidation(QJsonObject{{"message", "Error"}, {"severity", 2.5}}));
        for (const auto severity : {InputBoxValidationSeverity::Info, InputBoxValidationSeverity::Warning, InputBoxValidationSeverity::Error}) {
            const InputBoxValidationResult value(InputBoxValidationMessage{"", severity});
            const auto result = wire::decodeInputBoxValidation(wire::encodeInputBoxValidation(value));
            QVERIFY(result);
            QCOMPARE(std::get<InputBoxValidationMessage>(*result).severity, severity);
        }
    }
    void cancellationAndShutdownSettleFutures() {
        QVector<QJsonObject> sent;
        HostInputBox host([&](const QJsonObject& message) { sent.append(message); });
        CancellationTokenSource cancelled;
        cancelled.cancel();
        auto pre = host.show({}, cancelled.token());
        QVERIFY(pre.isFinished()); QVERIFY(!pre.result()); QVERIFY(sent.isEmpty());
        CancellationTokenSource source;
        auto future = host.show({}, source.token());
        QTRY_COMPARE(sent.size(), 1);
        source.cancel();
        QTRY_VERIFY(future.isFinished()); QVERIFY(!future.result());
        QCOMPARE(sent.last().value("op").toString(), QString("cancel"));
        auto pending = host.show({}, {});
        host.shutdown();
        QVERIFY(pending.isFinished());
        QVERIFY_EXCEPTION_THROWN(pending.result(), std::exception);
        QVERIFY(!host.accept(QJsonObject{{"requestId", "01"}, {"op", "result"}, {"value", QJsonValue(QJsonValue::Null)}, {"error", ""}}));
    }
    void sharedPopupPreservesWhitespaceSelectionAndPassword() {
        WorkbenchInputBox backend;
        HostInputBox* receiver = nullptr;
        HostInputBox host([&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture", message, [&](const QJsonObject& reply) { QVERIFY(receiver->accept(reply)); }));
        });
        receiver = &host;
        QWidget window; window.resize(800, 600); window.show();
        litecode::ui::InputBoxService service(backend, &window, &window);
        InputBoxOptions options; options.value = "  secret  "; options.password = true;
        options.ignoreFocusOut = true; options.valueSelection = QPair{2, 8};
        auto future = host.show(options, {});
        QTRY_VERIFY(window.findChild<litecode::ui::components::QuickInputDialog*>());
        auto* dialog = window.findChild<litecode::ui::components::QuickInputDialog*>();
        QCOMPARE(dialog->input()->echoMode(), QLineEdit::Password);
        QCOMPARE(dialog->input()->selectedText(), QString("secret"));
        dialog->accept();
        QTRY_VERIFY(future.isFinished());
        QVERIFY(future.result()); QCOMPARE(*future.result(), QString("  secret  "));
    }
    void asynchronousValidationIgnoresStaleErrorAndAcceptsWarning() {
        WorkbenchInputBox backend;
        HostInputBox* receiver = nullptr;
        HostInputBox host([&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture", message, [&](const QJsonObject& reply) { QVERIFY(receiver->accept(reply)); }));
        }); receiver = &host;
        QWidget window; window.resize(800, 600); window.show();
        litecode::ui::InputBoxService service(backend, &window, &window);
        QVector<std::shared_ptr<QPromise<InputBoxValidationResult>>> validations;
        InputBoxOptions options; options.value = "old"; options.ignoreFocusOut = true;
        options.validateInput = [&](const QString&) -> InputBoxValidatorResult {
            auto promise = std::make_shared<QPromise<InputBoxValidationResult>>(); promise->start();
            validations.append(promise); return promise->future();
        };
        auto future = host.show(options, {});
        QTRY_COMPARE(validations.size(), 1);
        auto* dialog = window.findChild<litecode::ui::components::QuickInputDialog*>(); QVERIFY(dialog);
        dialog->input()->setText("new"); dialog->accept();
        QTRY_COMPARE(validations.size(), 2);
        validations[0]->addResult(InputBoxValidationResult(QString("stale error"))); validations[0]->finish();
        QCoreApplication::processEvents(); QVERIFY(!future.isFinished());
        validations[1]->addResult(InputBoxValidationResult(InputBoxValidationMessage{"Warning", InputBoxValidationSeverity::Warning}));
        validations[1]->finish();
        QTRY_VERIFY(future.isFinished()); QVERIFY(future.result()); QCOMPARE(*future.result(), QString("new"));
    }
    void errorWithEmptyMessageBlocksAcceptanceButEmptyInputIsValidWithoutValidator() {
        WorkbenchInputBox backend;
        HostInputBox* receiver = nullptr;
        HostInputBox host([&](const QJsonObject& message) {
            QVERIFY(backend.accept("fixture", message, [&](const QJsonObject& reply) { QVERIFY(receiver->accept(reply)); }));
        }); receiver = &host;
        QWidget window; window.resize(800, 600); window.show();
        litecode::ui::InputBoxService service(backend, &window, &window);
        InputBoxOptions options; options.ignoreFocusOut = true;
        options.validateInput = [](const QString&) -> InputBoxValidatorResult {
            return InputBoxValidationResult(InputBoxValidationMessage{"", InputBoxValidationSeverity::Error});
        };
        QSignalSpy validated(&backend, &WorkbenchInputBox::validated);
        auto blocked = host.show(options, {});
        QTRY_COMPARE(validated.size(), 1);
        auto* dialog = window.findChild<litecode::ui::components::QuickInputDialog*>(); QVERIFY(dialog);
        dialog->accept(); QVERIFY(!blocked.isFinished()); dialog->reject();
        QTRY_VERIFY(blocked.isFinished()); QVERIFY(!blocked.result());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        options.validateInput = {};
        auto empty = host.show(options, {});
        QTRY_VERIFY(window.findChild<litecode::ui::components::QuickInputDialog*>());
        window.findChild<litecode::ui::components::QuickInputDialog*>()->accept();
        QTRY_VERIFY(empty.isFinished()); QVERIFY(empty.result()); QVERIFY(empty.result()->isEmpty());
    }
};
QTEST_MAIN(InputBoxTests)
#include "tst_extension_input_box.moc"
