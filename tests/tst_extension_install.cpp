#include "extensions/ExtensionManager.h"
#include "extensions/NativeExtensionInstaller.h"
#include "extensions/NativeExtensionUninstaller.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode::extensions;
class ExtensionInstallTests final : public QObject {
    Q_OBJECT
  private slots:
    void looseLibraryRemovalKeepsItsNeighbours() {
        QTemporaryDir installed;
        const auto library = installed.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, library));
        QFile neighbour(installed.filePath("keep.txt"));
        QVERIFY(neighbour.open(QIODevice::WriteOnly));
        neighbour.write("keep");
        neighbour.close();
        ExtensionManager manager(installed.path());
        manager.discover();
        QSignalSpy removed(&manager, &ExtensionManager::uninstallFinished);
        QVERIFY(manager.uninstall("tests.native"));
        QTRY_COMPARE(removed.size(), 1);
        QVERIFY2(removed[0][1].toBool(), qPrintable(removed[0][2].toString()));
        QVERIFY(!QFileInfo::exists(library));
        QVERIFY(QFileInfo::exists(neighbour.fileName()));
        QVERIFY(QFileInfo::exists(installed.path()));
    }
    void uninstallStopsHostRemovesPackageKeepsStateAndAllowsReimport() {
        QTemporaryDir installed;
        const auto imported = installNativeExtension(LITECODE_TEST_PLUGIN, installed.path());
        QVERIFY2(imported.error.isEmpty(), qPrintable(imported.error));
        QVERIFY(QDir().mkpath(installed.filePath("state/tests.native")));
        QFile state(installed.filePath("state/tests.native/history.txt"));
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write("keep history");
        state.close();
        ExtensionManager manager(installed.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy replies(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.hello"));
        QTRY_COMPARE(replies.size(), 1);
        QSignalSpy removed(&manager, &ExtensionManager::uninstallFinished);
        QVERIFY(manager.uninstall("tests.native"));
        QVERIFY(!manager.setEnabled("tests.native", true));
        QVERIFY(!manager.uninstall("tests.native"));
        QTRY_COMPARE_WITH_TIMEOUT(removed.size(), 1, 15000);
        QVERIFY2(removed[0][1].toBool(), qPrintable(removed[0][2].toString()));
        QVERIFY(manager.extensions().isEmpty());
        QVERIFY(!QFileInfo::exists(QFileInfo(imported.path).absolutePath()));
        QVERIFY(QFileInfo::exists(state.fileName()));
        QSettings settings(installed.filePath("enabled.ini"), QSettings::IniFormat);
        QVERIFY(!settings.contains("tests.native/trustedHash"));
        const auto reimport = installNativeExtension(LITECODE_TEST_PLUGIN, installed.path());
        QVERIFY2(reimport.error.isEmpty(), qPrintable(reimport.error));
        manager.discover();
        QCOMPARE(manager.extensions().size(), 1);
        QVERIFY(!manager.extensions().first().enabled);
        // The removed owner's activation timeout must not retain a dangling Entry*.
        QTest::qWait(10500);
    }
    void uninstallRefusesOutsideAndUnmanagedFoldersAndPreservesOtherExtension() {
        QTemporaryDir installed, outside;
        const auto name = QFileInfo(LITECODE_TEST_PLUGIN).fileName();
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, outside.filePath(name)));
        QVERIFY(!removeNativeExtension(outside.filePath(name), installed.path(), "tests.native",
                                       "1.0.0")
                     .removed);
        QVERIFY(QFileInfo::exists(outside.filePath(name)));
        QVERIFY(QDir().mkpath(installed.filePath("state")));
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, installed.filePath("state/" + name)));
        QVERIFY(!removeNativeExtension(installed.filePath("state/" + name), installed.path(),
                                       "tests.native", "1.0.0")
                     .removed);
        QVERIFY(QFileInfo::exists(installed.filePath("state/" + name)));
        const auto first = installNativeExtension(LITECODE_TEST_PLUGIN, installed.path());
        // The intentionally unmanaged library above has the same identity.
        QVERIFY(!first.error.isEmpty());
        QVERIFY(QFile::remove(installed.filePath("state/" + name)));
        const auto owned = installNativeExtension(LITECODE_TEST_PLUGIN, installed.path());
        const auto other = installNativeExtension(LITECODE_OTHER_PLUGIN, installed.path());
        QVERIFY(owned.error.isEmpty());
        QVERIFY(other.error.isEmpty());
        ExtensionManager manager(installed.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.otherFormatter", true));
        QSignalSpy replies(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.otherFormatter.echo"));
        QTRY_COMPARE(replies.size(), 1);
        QSignalSpy removed(&manager, &ExtensionManager::uninstallFinished);
        QVERIFY(manager.uninstall("tests.native"));
        QTRY_COMPARE(removed.size(), 1);
        QVERIFY(removed[0][1].toBool());
        QVERIFY(QFileInfo::exists(other.path));
        QCOMPARE(manager.extensions().size(), 1);
        QVERIFY(manager.extensions().first().enabled);
        QVERIFY(manager.requestCommand("tests.otherFormatter.echo"));
        QTRY_COMPARE(replies.size(), 2);
    }
    void folderResourcesAreInstalledDisabledAndDuplicateDoesNotOverwrite() {
        QTemporaryDir source, installed;
        QVERIFY(source.isValid());
        QVERIFY(installed.isValid());
        const auto library = QFileInfo(LITECODE_TEST_PLUGIN).fileName();
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, source.filePath(library)));
        QVERIFY(QDir().mkpath(source.filePath("resources")));
        QFile asset(source.filePath("resources/readme.txt"));
        QVERIFY(asset.open(QIODevice::WriteOnly));
        QCOMPARE(asset.write("fixture"), qint64(7));
        asset.close();
        // A runtime/resource library is not another installed extension.
        QVERIFY(QFile::copy(
            LITECODE_OTHER_PLUGIN,
            source.filePath("resources/" + QFileInfo(LITECODE_OTHER_PLUGIN).fileName())));
        const auto result = installNativeExtension(source.path(), installed.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.id, QString("tests.native"));
        QVERIFY(QFileInfo::exists(QFileInfo(result.path).dir().filePath("resources/readme.txt")));
        ExtensionManager manager(installed.path());
        manager.discover();
        QCOMPARE(manager.extensions().size(), 1);
        QVERIFY(!manager.extensions().first().enabled);
        QVERIFY(!manager.extensions().first().active);
        const auto duplicate = installNativeExtension(source.path(), installed.path());
        QVERIFY(!duplicate.error.isEmpty());
        manager.discover();
        QCOMPARE(manager.extensions().size(), 1);
    }
    void incrementalImportDoesNotStopExistingHost() {
        QTemporaryDir installed;
        QVERIFY(installed.isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN,
                            installed.filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        ExtensionManager manager(installed.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.discover();
        QVERIFY(manager.setEnabled("tests.native", true));
        QSignalSpy replies(&manager, &ExtensionManager::commandResult);
        QVERIFY(manager.requestCommand("tests.native.hello"));
        QTRY_COMPARE(replies.size(), 1);
        const auto result = installNativeExtension(LITECODE_OTHER_PLUGIN, installed.path());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        manager.discover();
        QCOMPARE(manager.extensions().size(), 2);
        QVERIFY(manager.requestCommand("tests.native.hello"));
        QTRY_COMPARE(replies.size(), 2);
        QVERIFY(replies.last()[2].toString().isEmpty());
    }
    void invalidInputNeverCreatesAnInstalledExtension() {
        QTemporaryDir source, installed;
        QVERIFY(source.isValid());
        QVERIFY(installed.isValid());
        QVERIFY(!installNativeExtension(source.path(), installed.path()).error.isEmpty());
        QVERIFY(!installNativeExtension(source.filePath("missing.dll"), installed.path())
                     .error.isEmpty());
        ExtensionManager manager(installed.path());
        manager.discover();
        QVERIFY(manager.extensions().isEmpty());
    }
};
QTEST_GUILESS_MAIN(ExtensionInstallTests)
#include "tst_extension_install.moc"
