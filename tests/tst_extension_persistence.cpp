#include "extensions/ConfigurationService.h"
#include "extensions/ExtensionManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace litecode::extensions;
class ExtensionPersistenceTests final : public QObject {
    Q_OBJECT
    QJsonValue result_;
    QString error_;
    bool received_{};
    bool call(ExtensionManager& manager, const QString& command, const QJsonObject& argument) {
        result_ = QJsonValue::Undefined;
        error_.clear();
        received_ = false;
        QSignalSpy replies(&manager, &ExtensionManager::commandResult);
        const auto id = manager.requestCommand(command, {argument});
        if (!id) {
            error_ = "Fixture command is unavailable.";
            return false;
        }
        if (replies.isEmpty() && !replies.wait(15000)) {
            error_ = "Fixture command timed out.";
            return false;
        }
        if (replies.first()[0].toULongLong() != id) {
            error_ = "Unexpected fixture reply.";
            return false;
        }
        received_ = true;
        result_ = replies.first()[1].value<QJsonValue>();
        error_ = replies.first()[2].toString();
        return error_.isEmpty();
    }
    bool prepare(ExtensionManager& manager) {
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        return manager.setEnabled("tests.native", true);
    }
    static void coordinateClosedSettings(ExtensionManager& manager, const QString& root) {
        // This headless workbench has no open editors. Authorize only its own
        // test-profile files; an absent guard must never authorize a write.
        manager.configuration().setWriteGuard([root](const QString& path) {
            const auto relative = QDir(root).relativeFilePath(path);
            return relative.startsWith("../") || QDir::isAbsolutePath(relative)
                       ? QString("Settings path is outside the test profile.")
                       : QString();
        });
    }
  private slots:
    void settingsUpdatesCrossHostsPersistAndRespectWriteCoordination() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        QVERIFY(QFile::copy(LITECODE_OTHER_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_OTHER_PLUGIN).fileName())));
        const auto workspace = directory.filePath("workspace");
        QVERIFY(QDir().mkpath(workspace));
        {
            ExtensionManager manager(directory.path());
            QVERIFY(prepare(manager));
            manager.setWorkspace(workspace);
            QVERIFY(manager.setEnabled("tests.otherFormatter", true));
            QVERIFY2(call(manager, "tests.native.configuration", {{"operation", "read"}}),
                     qPrintable(error_));
            QCOMPARE(result_.toObject().value("value").toString(), QString("default-value"));
            QVERIFY(result_.toObject().value("has").toBool());
            // Activate both hosts before changing the shared profile setting.
            QVERIFY2(call(manager, "tests.otherFormatter.configuration", {{"operation", "read"}}),
                     qPrintable(error_));
            QVERIFY(!call(manager, "tests.native.configuration",
                          {{"operation", "update"}, {"value", "not-authorized"}}));
            QVERIFY(received_);
            QVERIFY(error_.contains("coordination"));
            QVERIFY(!QFileInfo::exists(directory.filePath("state/User/settings.json")));
            coordinateClosedSettings(manager, directory.path());
            QVERIFY2(call(manager, "tests.native.configuration",
                          {{"operation", "update"}, {"value", "global-value"}}),
                     qPrintable(error_));
            QVERIFY2(call(manager, "tests.otherFormatter.configuration", {{"operation", "read"}}),
                     qPrintable(error_));
            QCOMPARE(result_.toObject().value("value").toString(), QString("global-value"));
            QCOMPARE(result_.toObject().value("globalValue").toString(), QString("global-value"));
            QVERIFY2(call(manager, "tests.otherFormatter.configuration", {{"operation", "events"}}),
                     qPrintable(error_));
            QVERIFY(result_.toArray().contains("fixtureRelease.value"));
            QVERIFY2(
                call(manager, "tests.native.configuration",
                     {{"operation", "update"}, {"workspace", true}, {"value", "workspace-value"}}),
                qPrintable(error_));
            QVERIFY2(call(manager, "tests.native.configuration", {{"operation", "read"}}),
                     qPrintable(error_));
            QCOMPARE(result_.toObject().value("value").toString(), QString("workspace-value"));
            // Upstream configuration editing validates keys/scopes and JSON,
            // not the value's schema type. Do not invent stricter SDK behavior.
            QVERIFY2(call(manager, "tests.native.configuration",
                          {{"operation", "update"}, {"value", 42}}),
                     qPrintable(error_));
            QVERIFY2(call(manager, "tests.native.configuration", {{"operation", "read"}}),
                     qPrintable(error_));
            QCOMPARE(result_.toObject().value("globalValue").toInt(), 42);
            QVERIFY2(call(manager, "tests.native.configuration",
                          {{"operation", "update"}, {"value", "global-value"}}),
                     qPrintable(error_));
            // Unsaved workbench settings must not be overwritten by a host.
            manager.configuration().setWriteGuard(
                [](const QString&) { return QString("Settings have unsaved changes."); });
            QVERIFY(!call(manager, "tests.native.configuration",
                          {{"operation", "update"}, {"value", "must-not-write"}}));
            QVERIFY(received_);
            QVERIFY(error_.contains("unsaved"));
            QVERIFY2(call(manager, "tests.native.configuration", {{"operation", "read"}}),
                     qPrintable(error_));
            QCOMPARE(result_.toObject().value("globalValue").toString(), QString("global-value"));
        }
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        manager.setWorkspace(workspace);
        coordinateClosedSettings(manager, directory.path());
        QVERIFY2(call(manager, "tests.native.configuration", {{"operation", "read"}}),
                 qPrintable(error_));
        QCOMPARE(result_.toObject().value("value").toString(), QString("workspace-value"));
        QCOMPARE(result_.toObject().value("globalValue").toString(), QString("global-value"));
        QVERIFY2(call(manager, "tests.native.configuration",
                      {{"operation", "update"}, {"workspace", true}}),
                 qPrintable(error_));
        QVERIFY2(call(manager, "tests.native.configuration", {{"operation", "read"}}),
                 qPrintable(error_));
        QCOMPARE(result_.toObject().value("value").toString(), QString("global-value"));
    }
    void mementosPersistAcrossHostsAndIsolateWorkspaceState() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        const auto workspaceA = directory.filePath("workspace-a");
        const auto workspaceB = directory.filePath("workspace-b");
        QVERIFY(QDir().mkpath(workspaceA));
        QVERIFY(QDir().mkpath(workspaceB));
        QString storageA, globalStorage;
        {
            ExtensionManager manager(directory.path());
            QVERIFY(prepare(manager));
            manager.setWorkspace(workspaceA);
            QVERIFY2(call(manager, "tests.native.persistence",
                          {{"operation", "globalUpdate"}, {"value", "global-value"}}),
                     qPrintable(error_));
            QVERIFY2(call(manager, "tests.native.persistence",
                          {{"operation", "workspaceUpdate"}, {"value", "workspace-a-value"}}),
                     qPrintable(error_));
            QVERIFY2(call(manager, "tests.native.persistence", {{"operation", "syncKeys"}}),
                     qPrintable(error_));
            QVERIFY2(call(manager, "tests.native.persistence", {{"operation", "read"}}),
                     qPrintable(error_));
            const auto state = result_.toObject();
            QCOMPARE(state.value("global").toString(), QString("global-value"));
            QCOMPARE(state.value("workspace").toString(), QString("workspace-a-value"));
            QVERIFY(state.value("globalKeys").toArray().contains("value"));
            storageA = state.value("storageUri").toString();
            globalStorage = state.value("globalStorageUri").toString();
            QVERIFY(QUrl(storageA).isLocalFile());
            QVERIFY(QUrl(globalStorage).isLocalFile());
        }
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.extensions().first().enabled);
        manager.setWorkspace(workspaceA);
        QVERIFY2(call(manager, "tests.native.persistence", {{"operation", "read"}}),
                 qPrintable(error_));
        QCOMPARE(result_.toObject().value("global").toString(), QString("global-value"));
        QCOMPARE(result_.toObject().value("workspace").toString(), QString("workspace-a-value"));
        QCOMPARE(result_.toObject().value("storageUri").toString(), storageA);
        manager.setWorkspace(workspaceB);
        QVERIFY2(call(manager, "tests.native.persistence", {{"operation", "read"}}),
                 qPrintable(error_));
        QVERIFY(result_.toObject().value("workspace").isUndefined());
        QCOMPARE(result_.toObject().value("globalStorageUri").toString(), globalStorage);
        QVERIFY(result_.toObject().value("storageUri").toString() != storageA);
        QVERIFY2(call(manager, "tests.native.persistence",
                      {{"operation", "workspaceUpdate"}, {"value", "workspace-b-value"}}),
                 qPrintable(error_));
        manager.setWorkspace(workspaceA);
        QVERIFY2(call(manager, "tests.native.persistence", {{"operation", "read"}}),
                 qPrintable(error_));
        QCOMPARE(result_.toObject().value("workspace").toString(), QString("workspace-a-value"));
        QVERIFY2(call(manager, "tests.native.persistence", {{"operation", "globalUpdate"}}),
                 qPrintable(error_));
        QVERIFY2(call(manager, "tests.native.persistence", {{"operation", "read"}}),
                 qPrintable(error_));
        QVERIFY(result_.toObject().value("global").isUndefined());
        QVERIFY(!result_.toObject().value("globalKeys").toArray().contains("value"));
    }
    void credentialsRoundTripPersistAndRemainExtensionScoped() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        QVERIFY(QFile::copy(LITECODE_OTHER_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_OTHER_PLUGIN).fileName())));
        const QString token("fixture-token-not-a-real-credential");
        {
            ExtensionManager manager(directory.path());
            QVERIFY(prepare(manager));
            QVERIFY(manager.setEnabled("tests.otherFormatter", true));
            QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "get"}}),
                     qPrintable(error_));
            QVERIFY(!result_.toObject().value("found").toBool());
            QVERIFY2(
                call(manager, "tests.native.secrets", {{"operation", "store"}, {"value", token}}),
                qPrintable(error_));
            QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "get"}}),
                     qPrintable(error_));
            QCOMPARE(result_.toObject().value("value").toString(), token);
            QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "events"}}),
                     qPrintable(error_));
            QVERIFY(result_.toArray().contains("token"));
            QVERIFY2(call(manager, "tests.otherFormatter.secrets", {{"operation", "get"}}),
                     qPrintable(error_));
            QVERIFY(!result_.toObject().value("found").toBool());
            QFile encrypted(directory.filePath("state/secrets/tests.native.json"));
            QVERIFY(encrypted.open(QIODevice::ReadOnly));
            QVERIFY(!encrypted.readAll().contains(token.toUtf8()));
        }
        ExtensionManager manager(directory.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "get"}}), qPrintable(error_));
        QCOMPARE(result_.toObject().value("value").toString(), token);
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "store"}, {"value", ""}}),
                 qPrintable(error_));
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "get"}}), qPrintable(error_));
        QVERIFY(result_.toObject().value("found").toBool());
        QCOMPARE(result_.toObject().value("value").toString(), QString());
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "keys"}}),
                 qPrintable(error_));
        QVERIFY(result_.toArray().contains("token"));
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "delete"}}),
                 qPrintable(error_));
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "get"}}), qPrintable(error_));
        QVERIFY(!result_.toObject().value("found").toBool());
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "events"}}),
                 qPrintable(error_));
        const auto events = result_.toArray();
        QVERIFY(std::count(events.cbegin(), events.cend(), QJsonValue("token")) >= 2);
    }
    void credentialChangesReachAnotherWindowHost() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager first(directory.path());
        QVERIFY(prepare(first));
        ExtensionManager second(directory.path());
        second.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        second.discover();
        // Activate both real hosts before the first write so the second host
        // must observe a live file replacement, not just its initial snapshot.
        QVERIFY2(call(first, "tests.native.secrets", {{"operation", "get"}}), qPrintable(error_));
        QVERIFY2(call(second, "tests.native.secrets", {{"operation", "get"}}), qPrintable(error_));
        QVERIFY2(call(second, "tests.native.secrets", {{"operation", "events"}}),
                 qPrintable(error_));
        QVERIFY(result_.toArray().isEmpty());
        QVERIFY2(call(first, "tests.native.secrets",
                      {{"operation", "store"}, {"value", "cross-window-test-only"}}),
                 qPrintable(error_));
        const auto observed = [&](int minimum) {
            if (!call(second, "tests.native.secrets", {{"operation", "events"}}))
                return false;
            const auto events = result_.toArray();
            return std::count(events.cbegin(), events.cend(), QJsonValue("token")) >= minimum;
        };
        QTRY_VERIFY_WITH_TIMEOUT(observed(1), 5000);
        QVERIFY2(call(second, "tests.native.secrets", {{"operation", "get"}}), qPrintable(error_));
        QCOMPARE(result_.toObject().value("value").toString(), QString("cross-window-test-only"));
        QVERIFY2(call(first, "tests.native.secrets", {{"operation", "delete"}}),
                 qPrintable(error_));
        QTRY_VERIFY_WITH_TIMEOUT(observed(2), 5000);
        QVERIFY2(call(second, "tests.native.secrets", {{"operation", "get"}}), qPrintable(error_));
        QVERIFY(!result_.toObject().value("found").toBool());
    }
    void credentialReadErrorsPreserveCiphertextAndDoNotBlockLaterRequests() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            directory.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(directory.path());
        QVERIFY(prepare(manager));
        const QString token("failure-fixture-not-a-real-credential");
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "store"}, {"value", token}}),
                 qPrintable(error_));
        const auto path = directory.filePath("state/secrets/tests.native.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = file.readAll();
        file.close();
        const auto write = [&path](const QByteArray& bytes) {
            QSaveFile destination(path);
            return destination.open(QIODevice::WriteOnly) &&
                   destination.write(bytes) == bytes.size() && destination.commit();
        };
        auto document = QJsonDocument::fromJson(original).object();
        auto values = document.value("values").toObject();
        auto cipher = QByteArray::fromBase64(values.value("token").toString().toLatin1());
        QVERIFY(!cipher.isEmpty());
        cipher[cipher.size() - 1] = static_cast<char>(cipher.back() ^ 1);
        values.insert("token", QString::fromLatin1(cipher.toBase64()));
        document.insert("values", values);
        const auto tampered = QJsonDocument(document).toJson(QJsonDocument::Compact);
        values.insert("token", "!not-base64!");
        document.insert("values", values);
        const auto invalidCipher = QJsonDocument(document).toJson(QJsonDocument::Compact);
        for (const auto& damaged : {tampered, invalidCipher, QByteArray("{invalid-json")}) {
            QVERIFY(write(damaged));
            QVERIFY(!call(manager, "tests.native.secrets", {{"operation", "get"}}));
            QVERIFY(received_);
            QVERIFY(!error_.isEmpty());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), damaged);
            file.close();
            // A read/decryption failure must neither erase the stored value nor
            // poison the per-key operation queue. Restore only our test data.
            QVERIFY(write(original));
            QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "get"}}),
                     qPrintable(error_));
            QCOMPARE(result_.toObject().value("value").toString(), token);
        }
        QVERIFY2(call(manager, "tests.native.secrets", {{"operation", "delete"}}),
                 qPrintable(error_));
    }
};
QTEST_GUILESS_MAIN(ExtensionPersistenceTests)
#include "tst_extension_persistence.moc"
