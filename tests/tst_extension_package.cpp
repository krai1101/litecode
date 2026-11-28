#include "extensions/ExtensionManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace litecode::extensions;
namespace {
QString packagedHost() {
    const auto package = qEnvironmentVariable("LITECODE_PACKAGE_ROOT");
#ifdef Q_OS_MACOS
    const auto bin = QDir(package).filePath("LiteCode.app/Contents/MacOS");
#else
    const auto bin = QDir(package).filePath("bin");
#endif
#ifdef Q_OS_WIN
    return QDir(bin).filePath("LiteCodeExtensionHost.exe");
#else
    return QDir(bin).filePath("LiteCodeExtensionHost");
#endif
}
class PackageRuntimeEnvironment final {
  public:
    PackageRuntimeEnvironment() {
        for (const auto* key :
             {"PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH",
              "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"}) {
            saved_.insert(key, qgetenv(key));
            qunsetenv(key);
        }
#ifdef Q_OS_WIN
        const auto system = qEnvironmentVariable("SystemRoot");
        qputenv("PATH", (system + "/System32;" + system).toUtf8());
#else
        qputenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin");
#endif
    }
    ~PackageRuntimeEnvironment() {
        for (auto it = saved_.cbegin(); it != saved_.cend(); ++it) {
            if (it.value().isNull())
                qunsetenv(it.key().constData());
            else
                qputenv(it.key().constData(), it.value());
        }
    }

  private:
    QHash<QByteArray, QByteArray> saved_;
};
} // namespace
class ExtensionPackageTests final : public QObject {
    Q_OBJECT
    QJsonValue result_;
    QString error_;
    bool call(ExtensionManager& manager, const QJsonObject& request) {
        QSignalSpy replies(&manager, &ExtensionManager::commandResult);
        const auto id = manager.requestCommand("tests.native.secrets", {request});
        if (!id) {
            error_ = "Packaged fixture command is unavailable.";
            return false;
        }
        if (replies.isEmpty() && !replies.wait(15000)) {
            error_ = "Packaged fixture request timed out.";
            return false;
        }
        if (replies.first()[0].toULongLong() != id) {
            error_ = "Unexpected fixture response.";
            return false;
        }
        result_ = replies.first()[1].value<QJsonValue>();
        error_ = replies.first()[2].toString();
        return error_.isEmpty();
    }
  private slots:
    void independentExtensionRunsInPackagedHostWithoutDevelopmentRuntime() {
        const auto package = qEnvironmentVariable("LITECODE_PACKAGE_ROOT");
        const auto plugin = qEnvironmentVariable("LITECODE_PACKAGE_PLUGIN");
        if (package.isEmpty() && plugin.isEmpty())
            QSKIP("Run after staging the package and independently building its SDK example.");
        QVERIFY2(QFileInfo(package).isDir(), "A staged package is required.");
        QVERIFY2(QFileInfo(plugin).isFile(), "An independently built SDK example is required.");
        const auto host = packagedHost();
        QVERIFY2(QFileInfo(host).isExecutable(), qPrintable(host));
        QTemporaryDir extensions;
        QVERIFY(extensions.isValid());
        QVERIFY(QFile::copy(plugin, extensions.filePath(QFileInfo(plugin).fileName())));
        // The test's own Qt is already loaded. The child must resolve the
        // packaged runtime, not borrow Qt/libraries from the build environment.
        const PackageRuntimeEnvironment runtime;
        ExtensionManager manager(extensions.path());
        manager.setHostExecutablePath(host);
        manager.discover();
        QCOMPARE(manager.extensions().size(), 1);
        QVERIFY2(manager.extensions().first().error.isEmpty(),
                 qPrintable(manager.extensions().first().error));
        QVERIFY(manager.setEnabled("example.hello", true));
        QSignalSpy results(&manager, &ExtensionManager::commandResult);
        QSignalSpy messages(&manager, &ExtensionManager::informationMessage);
        QVERIFY(manager.requestCommand("example.hello.greet"));
        QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 15000);
        QVERIFY2(results.first()[2].toString().isEmpty(),
                 qPrintable(results.first()[2].toString()));
        QTRY_COMPARE(messages.size(), 1);
        QCOMPARE(messages.first().first().toString(),
                 QString("Hello from a native LiteCode extension!"));
        QVERIFY(manager.extensions().first().active);
        QVERIFY(manager.setEnabled("example.hello", false));
        QTRY_VERIFY(!manager.extensions().first().active);
        QCOMPARE(manager.requestCommand("example.hello.greet"), quint64(0));
    }
    void credentialsPersistThroughThePackagedHostWithoutDevelopmentRuntime() {
        if (qEnvironmentVariableIsEmpty("LITECODE_PACKAGE_ROOT"))
            QSKIP("Run after staging the package.");
        const auto host = packagedHost();
        QVERIFY2(QFileInfo(host).isExecutable(), qPrintable(host));
        QTemporaryDir extensions;
        QVERIFY(extensions.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            extensions.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        const PackageRuntimeEnvironment runtime;
        const QString token("package-fixture-not-a-real-credential");
        {
            ExtensionManager manager(extensions.path());
            manager.setHostExecutablePath(host);
            manager.discover();
            QVERIFY(manager.setEnabled("tests.native", true));
            QVERIFY2(call(manager, {{"operation", "store"}, {"value", token}}), qPrintable(error_));
            QVERIFY2(call(manager, {{"operation", "get"}}), qPrintable(error_));
            QCOMPARE(result_.toObject().value("value").toString(), token);
            QFile encrypted(extensions.filePath("state/secrets/tests.native.json"));
            QVERIFY(encrypted.open(QIODevice::ReadOnly));
            QVERIFY(!encrypted.readAll().contains(token.toUtf8()));
        }
        ExtensionManager manager(extensions.path());
        manager.setHostExecutablePath(host);
        manager.discover();
        QVERIFY2(call(manager, {{"operation", "get"}}), qPrintable(error_));
        QCOMPARE(result_.toObject().value("value").toString(), token);
        QVERIFY2(call(manager, {{"operation", "delete"}}), qPrintable(error_));
        QVERIFY2(call(manager, {{"operation", "get"}}), qPrintable(error_));
        QVERIFY(!result_.toObject().value("found").toBool());
    }
};
QTEST_GUILESS_MAIN(ExtensionPackageTests)
#include "tst_extension_package.moc"
