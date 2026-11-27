#include "debug/TaskConfiguration.h"
#include "debug/TaskRunner.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode::debug;
class TaskConfigurationTests final : public QObject {
    Q_OBJECT
  private slots:
    void dependenciesAndResolution() {
        QString error;
        const auto tasks = TaskConfigurations::parse(R"({"version":"2.0.0","tasks":[
            {"label":"compile","type":"process","command":"compiler","args":["${file}","a & b"],
             "options":{"cwd":"build","env":{"REMOVE":null,"NEW":"${env:VALUE}"}}},
            {"label":"package","type":"process","command":"pack","dependsOn":"compile"},
            {"label":"all","dependsOn":["compile","package"],"dependsOrder":"sequence",
             "group":{"kind":"build","isDefault":true}}
        ]})",
                                                     &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto graph =
            TaskConfigurations::dependencyGraph(tasks, "${defaultBuildTask}", &error);
        QCOMPARE(graph.size(), 3);
        QCOMPARE(graph[0].label, QString("compile"));
        QCOMPARE(graph[1].label, QString("package"));
        QCOMPARE(graph[2].label, QString("all"));
        QVERIFY(graph[2].sequential);
        VariableContext context;
        context.workspaceFolder = "C:/project";
        context.file = "C:/project/file with spaces.cpp";
        context.environment.insert("VALUE", "literal");
        context.environment.insert("REMOVE", "old");
        const auto command = TaskConfigurations::resolve(graph[0], context, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(command.arguments, QStringList({context.file, "a & b"}));
        QCOMPARE(command.cwd, QDir(context.workspaceFolder).absoluteFilePath("build"));
        QVERIFY(!command.environment.contains("REMOVE"));
        QCOMPARE(command.environment.value("NEW"), QString("literal"));
    }
    void graphFailures() {
        QString error;
        const auto tasks = TaskConfigurations::parse(R"({"version":"2.0.0","tasks":[
            {"label":"a","dependsOn":"b"},{"label":"b","dependsOn":"a"}
        ]})",
                                                     &error);
        QVERIFY(error.isEmpty());
        QVERIFY(TaskConfigurations::dependencyGraph(tasks, "a", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(TaskConfigurations::dependencyGraph(tasks, "missing", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(
            TaskConfigurations::dependencyGraph(tasks, "${defaultBuildTask}", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void vscodeTasksTakePriorityAndRunDependencies() {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QVERIFY(QDir(workspace.path()).mkdir(".litecode"));
        QFile legacy(workspace.filePath(".litecode/tasks.json"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(
            R"({"version":"2.0.0","tasks":[{"label":"legacy","type":"process","command":"legacy"}]})");
        legacy.close();
        QFile current(workspace.filePath(".vscode/tasks.json"));
        QVERIFY(current.open(QIODevice::WriteOnly));
#ifdef Q_OS_WIN
        const QByteArray tasks = R"({"version":"2.0.0","tasks":[
          {"label":"first","type":"process","command":"cmd.exe","args":["/c","echo first"]},
          {"label":"second","type":"process","command":"cmd.exe","args":["/c","echo second"],"dependsOn":"first"}
        ]})";
#else
        const QByteArray tasks = R"({"version":"2.0.0","tasks":[
          {"label":"first","type":"process","command":"/bin/echo","args":["first"]},
          {"label":"second","type":"process","command":"/bin/echo","args":["second"],"dependsOn":"first"}
        ]})";
#endif
        current.write(tasks);
        current.close();
        QString error;
        const auto definitions = TaskConfigurations::load(workspace.path(), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(definitions.size(), 2);
        QCOMPARE(definitions.first().label, QString("first"));
        TaskRunner runner;
        QString output;
        QObject::connect(&runner, &TaskRunner::outputReceived, &runner,
                         [&output](const QString& text) { output += text; });
        QSignalSpy finished(&runner, &TaskRunner::finished);
        VariableContext context;
        context.workspaceFolder = workspace.path();
        context.environment = QProcessEnvironment::systemEnvironment();
        QVERIFY(runner.start(workspace.path(), "second", context));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(finished.first().first().toBool());
        QVERIFY(output.indexOf("first") < output.indexOf("second"));
    }
    void failedTaskStopsBeforeDependent() {
        QTemporaryDir workspace;
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QFile file(workspace.filePath(".vscode/tasks.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
#ifdef Q_OS_WIN
        const QByteArray tasks = R"({"version":"2.0.0","tasks":[
          {"label":"fail","type":"process","command":"cmd.exe","args":["/c","exit 3"]},
          {"label":"later","type":"process","command":"cmd.exe","args":["/c","echo later"],"dependsOn":"fail"}
        ]})";
#else
        const QByteArray tasks = R"({"version":"2.0.0","tasks":[
          {"label":"fail","type":"process","command":"/bin/sh","args":["-c","exit 3"]},
          {"label":"later","type":"process","command":"/bin/echo","args":["later"],"dependsOn":"fail"}
        ]})";
#endif
        file.write(tasks);
        file.close();
        TaskRunner runner;
        QString output;
        QObject::connect(&runner, &TaskRunner::outputReceived, &runner,
                         [&output](const QString& text) { output += text; });
        QSignalSpy finished(&runner, &TaskRunner::finished);
        VariableContext context;
        context.workspaceFolder = workspace.path();
        context.environment = QProcessEnvironment::systemEnvironment();
        QVERIFY(runner.start(workspace.path(), "later", context));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!finished.first().first().toBool());
        QVERIFY(finished.first().at(1).toString().contains("3"));
        QVERIFY(!output.contains("> Task: later"));
    }

    void cancellingTaskAllowsRestartAfterProcessStops() {
        QTemporaryDir workspace;
        QVERIFY(QDir(workspace.path()).mkdir(".vscode"));
        QFile file(workspace.filePath(".vscode/tasks.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
#ifdef Q_OS_WIN
        file.write(R"({"version":"2.0.0","tasks":[
          {"label":"wait","type":"process","command":"ping.exe","args":["-n","15","127.0.0.1"]},
          {"label":"next","type":"process","command":"cmd.exe","args":["/c","exit 0"]}
        ]})");
#else
        file.write(R"({"version":"2.0.0","tasks":[
          {"label":"wait","type":"process","command":"/bin/sleep","args":["15"]},
          {"label":"next","type":"process","command":"/bin/true"}
        ]})");
#endif
        file.close();
        TaskRunner runner;
        QSignalSpy finished(&runner, &TaskRunner::finished);
        VariableContext context;
        context.workspaceFolder = workspace.path();
        context.environment = QProcessEnvironment::systemEnvironment();
        QVERIFY(runner.start(workspace.path(), "wait", context));
        runner.stop();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.first().first().toBool());
        QCOMPARE(finished.first().at(1).toString(), QString("Task cancelled."));
        QVERIFY(!runner.active());
        QVERIFY(runner.start(workspace.path(), "next", context));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QVERIFY(finished.last().first().toBool());
    }
};
QTEST_GUILESS_MAIN(TaskConfigurationTests)
#include "tst_task_configuration.moc"
