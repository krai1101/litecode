#include "debug/DebugConfiguration.h"
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode::debug;
class DebugConfigurationTests final : public QObject {
    Q_OBJECT
  private slots:
    void loadsVscodeLaunchBeforeLegacyLitecodeFile() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QCOMPARE(DebugConfigurations::filePath(workspace.path()),
                 workspace.filePath(".vscode/launch.json"));
        QFile launch(DebugConfigurations::filePath(workspace.path()));
        QVERIFY(launch.open(QIODevice::WriteOnly));
        const QByteArray bytes =
            R"({"version":"0.2.0","configurations":[{"name":"VS Code","type":"test","request":"launch"}]})";
        QCOMPARE(launch.write(bytes), qint64(bytes.size()));
        launch.close();
        QString error;
        const auto configurations = DebugConfigurations::load(workspace.path(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(configurations.size(), 1);
        QCOMPARE(configurations.first().name, QString("VS Code"));
    }
    void jsoncAndPlatformOverrides() {
        QString error;
        // Ordinary string literals keep JSONC comments opaque to Qt's moc lexer.
        const auto configurations = DebugConfigurations::parse(
            "{\n"
            "  // Familiar launch schema, with native provider types.\n"
            "  \"version\": \"0.2.0\",\n"
            "  \"configurations\": [{\n"
            "    \"name\": \"C++\", \"type\": \"lldb-dap\", \"request\": \"launch\",\n"
            "    \"program\": \"${workspaceFolder}/app\",\n"
            "    \"args\": [\"https://example.test/a/*literal*/\", \"a,b\",],\n"
            "    \"windows\": {\"program\": \"${workspaceFolder}/app.exe\",},\n"
            "  },], /* end */\n"
            "}",
            &error, "windows");
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(configurations.size(), 1);
        QCOMPARE(configurations[0].values.value("program").toString(),
                 QString("${workspaceFolder}/app.exe"));
        QCOMPARE(configurations[0].values.value("args").toArray()[0].toString(),
                 QString("https://example.test/a/*literal*/"));
        QVERIFY(!configurations[0].values.contains("windows"));
    }
    void validationFailures_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("array-root") << QByteArray("[]");
        QTest::newRow("comment") << QByteArray("{/* unfinished");
        QTest::newRow("missing-configurations") << QByteArray("{}");
        QTest::newRow("missing-type")
            << QByteArray(R"({"configurations":[{"name":"x","request":"launch"}]})");
        QTest::newRow("bad-request") << QByteArray(
            R"({"configurations":[{"name":"x","type":"cpp","request":"invalid"}]})");
        QTest::newRow("too-large") << QByteArray(1024 * 1024 + 1, ' ');
    }
    void validationFailures() {
        QFETCH(QByteArray, json);
        QString error;
        QVERIFY(DebugConfigurations::parse(json, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void variableSubstitutionIsLiteral() {
        VariableContext context;
        context.workspaceFolder = "C:/Project with spaces";
        context.file = "C:/Project with spaces/main.cpp";
        context.environment.insert("TEST_VALUE", "${literal} & | 'quoted'");
        QString error;
        const auto resolved =
            DebugConfigurations::resolve(
                QJsonObject{{"program", "${workspaceFolder}/${fileBasenameNoExtension}"},
                            {"args", QJsonArray{"${env:TEST_VALUE}", "${fileExtname}"}},
                            {"env", QJsonObject{{"REMOVE", QJsonValue(QJsonValue::Null)}}}},
                context, &error)
                .toObject();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(resolved.value("program").toString(), QString("C:/Project with spaces/main"));
        QCOMPARE(resolved.value("args").toArray()[0].toString(),
                 QString("${literal} & | 'quoted'"));
        QCOMPARE(resolved.value("args").toArray()[1].toString(), QString(".cpp"));
        QVERIFY(resolved.value("env").toObject().value("REMOVE").isNull());
        DebugConfigurations::resolve("${command:unimplemented}", context, &error);
        QVERIFY(!error.isEmpty());
        DebugConfigurations::resolve("${workspaceFolder", context, &error);
        QVERIFY(!error.isEmpty());
        context.file.clear();
        DebugConfigurations::resolve("${file}", context, &error);
        QVERIFY(!error.isEmpty());
    }
    void contributedDebuggerSchemaRejectsWrongTypes() {
        const QJsonObject schema{{"type", "object"},
                                 {"properties", QJsonObject{
                                      {"interpreter", QJsonObject{{"type", "string"}}},
                                      {"arguments", QJsonObject{{"type", "array"},
                                                                  {"items", QJsonObject{{"type", "string"}}}}}}}};
        QString error;
        QVERIFY(!DebugConfigurations::validateAgainstSchema(
            QJsonObject{{"interpreter", QJsonArray{"python"}}}, schema, &error));
        QVERIFY(error.contains(QStringLiteral("interpreter")));
        QVERIFY(error.contains(QStringLiteral("expected string")));
        QVERIFY(DebugConfigurations::validateAgainstSchema(
            QJsonObject{{"interpreter", "python"},
                        {"arguments", QJsonArray{"--version"}}}, schema, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!DebugConfigurations::validateAgainstSchema(
            QJsonObject{{"interpreter", "python"},
                        {"arguments", QJsonArray{12}}}, schema, &error));
        QVERIFY(error.contains(QStringLiteral("arguments[0]")));
    }
    void debuggerSpecificFieldsAreNotGloballyRejected() {
        QString error;
        const auto configurations = DebugConfigurations::parse(
            R"({"version":"0.2.0","configurations":[{"name":"Flexible","type":"fixture","request":"launch","args":"--flag","env":{"VALUE":42},"program":{"module":"example"}}]})",
            &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(configurations.size(), 1);
        QCOMPARE(configurations.first().values.value("args").toString(), QString("--flag"));
    }
};
QTEST_GUILESS_MAIN(DebugConfigurationTests)
#include "tst_debug_configuration.moc"
